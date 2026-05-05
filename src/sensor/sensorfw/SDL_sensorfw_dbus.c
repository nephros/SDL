/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL_internal.h"

#if defined(SDL_SENSOR_SENSORFW)

#include "SDL_sensorfw.h"
#include "../SDL_syssensor.h"

#include "../../core/linux/SDL_dbus.h"
#include <unistd.h>

/* copied straight from
 * https://github.com/sailfishos/sensors-glib/blob/main/sfwdbus.h
 *
 * TODO: shouldn't we make these into a struct/union?
 */
#define SENSORFW_SERVICE                          "com.nokia.SensorService"

#define SENSORFW_MANAGER_OBJECT                   "/SensorManager"
#define SENSORFW_MANAGER_IFACE                    "local.SensorManager"
#define SENSORFW_MANAGER_METHOD_LOAD_PLUGIN       "loadPlugin"
#define SENSORFW_MANAGER_METHOD_START_SESSION     "requestSensor"
#define SENSORFW_MANAGER_METHOD_STOP_SESSION      "releaseSensor"
#define SENSORFW_MANAGER_METHOD_AVAILABLE_PLUGINS "availableSensorPlugins"

#define SENSORFW_SENSOR_METHOD_START              "start"
#define SENSORFW_SENSOR_METHOD_STOP               "stop"
#define SENSORFW_SENSOR_METHOD_SET_OVERRIDE       "setStandbyOverride"
#define SENSORFW_SENSOR_METHOD_SET_DATARATE       "setDataRate"
#define SENSORFW_SENSOR_METHOD_GET_PROXIMITY      "proximity"
#define SENSORFW_SENSOR_METHOD_GET_ALS            "lux"
#define SENSORFW_SENSOR_METHOD_GET_ORIENTATION    "orientation"
#define SENSORFW_SENSOR_METHOD_GET_ACCELEROMETER  "xyz"
#define SENSORFW_SENSOR_METHOD_GET_COMPASS        "value" /* or "declinationvalue" */
#define SENSORFW_SENSOR_METHOD_GET_GYROSCOPE      "value"
#define SENSORFW_SENSOR_METHOD_GET_LID            "closed"
#define SENSORFW_SENSOR_METHOD_GET_HUMIDITY       "relativeHumidity"
#define SENSORFW_SENSOR_METHOD_GET_MAGNETOMETER   "magneticField"
#define SENSORFW_SENSOR_METHOD_GET_PRESSURE       "pressure"
#define SENSORFW_SENSOR_METHOD_GET_ROTATION       "rotation"
#define SENSORFW_SENSOR_METHOD_GET_STEPCOUNTER    "steps"
#define SENSORFW_SENSOR_METHOD_GET_TAP            NULL /* has no state. just events */
#define SENSORFW_SENSOR_METHOD_GET_TEMPERATURE    "temperature"

#define SENSORFW_SENSOR_NAME_PROXIMITY            "proximitysensor"
#define SENSORFW_SENSOR_NAME_ALS                  "alssensor"
#define SENSORFW_SENSOR_NAME_ORIENTATION          "orientationsensor"
#define SENSORFW_SENSOR_NAME_ACCELEROMETER        "accelerometersensor"
#define SENSORFW_SENSOR_NAME_COMPASS              "compasssensor"
#define SENSORFW_SENSOR_NAME_GYROSCOPE            "gyroscopesensor"
#define SENSORFW_SENSOR_NAME_LID                  "lidsensor"
#define SENSORFW_SENSOR_NAME_HUMIDITY             "humiditysensor"
#define SENSORFW_SENSOR_NAME_MAGNETOMETER         "magnetometersensor"
#define SENSORFW_SENSOR_NAME_PRESSURE             "pressuresensor"
#define SENSORFW_SENSOR_NAME_ROTATION             "rotationsensor"
#define SENSORFW_SENSOR_NAME_STEPCOUNTER          "stepcountersensor"
#define SENSORFW_SENSOR_NAME_TAP                  "tapsensor"
#define SENSORFW_SENSOR_NAME_TEMPERATURE          "temperaturesensor"

#define SENSORFW_SENSOR_INTERFACE_PROXIMITY       "local.ProximitySensor"
#define SENSORFW_SENSOR_INTERFACE_ALS             "local.ALSSensor"
#define SENSORFW_SENSOR_INTERFACE_ORIENTATION     "local.OrientationSensor"
#define SENSORFW_SENSOR_INTERFACE_ACCELEROMETER   "local.AccelerometerSensor"
#define SENSORFW_SENSOR_INTERFACE_COMPASS         "local.CompassSensor"
#define SENSORFW_SENSOR_INTERFACE_GYROSCOPE       "local.GyroscopeSensor"
#define SENSORFW_SENSOR_INTERFACE_LID             "local.LidSensor"
#define SENSORFW_SENSOR_INTERFACE_HUMIDITY        "local.HumiditySensor"
#define SENSORFW_SENSOR_INTERFACE_MAGNETOMETER    "local.MagnetometerSensor"
#define SENSORFW_SENSOR_INTERFACE_PRESSURE        "local.PressureSensor"
#define SENSORFW_SENSOR_INTERFACE_ROTATION        "local.RotationSensor"
#define SENSORFW_SENSOR_INTERFACE_STEPCOUNTER     "local.StepcounterSensor"
#define SENSORFW_SENSOR_INTERFACE_TAP             "local.TapSensor"
#define SENSORFW_SENSOR_INTERFACE_TEMPERATURE     "local.TemperatureSensor"

typedef struct
{
    SDL_SensorType type;
    const char*    name;
    SDL_SensorID   instance_id;

    const char*    plugin_name;
    const char*    interface_name;
    const char*    method_name;
    bool           plugin_loaded;
    int32_t        session;
} SDL_SensorFWSensor;

static SDL_SensorFWSensor *SDL_sensors;
static int SDL_sensors_count;

static void SensorFW_UpdateAccelDbus(SDL_Sensor *sensor);
static void SensorFW_UpdateGyroDbus(SDL_Sensor *sensor);

static bool SDL_SENSORFWDBUS_SensorInit(void)
{
    bool result = false;
    SDL_sensors_count = 0;

#ifdef SDL_USE_LIBDBUS

    // FIXME: we alloc exactly two sensors.
    // We have more, but SDL only supports accel and gyro anyway.
    SDL_sensors = (SDL_SensorFWSensor *)SDL_calloc(2, sizeof(*SDL_sensors));
    if (!SDL_sensors) {
        return false;
    }

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return false;
    }

    // list available plugins 
    DBusMessage *reply = NULL;
    if (!SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                         SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                         SENSORFW_MANAGER_METHOD_AVAILABLE_PLUGINS,
                                         DBUS_TYPE_INVALID))
    {
        // reply is of signature 'as'
        DBusMessageIter iter;
        DBusMessageIter array_iter;
        dbus->message_iter_init(reply, &iter);
        dbus->message_iter_recurse(&iter, &array_iter);
        int count = 0;
        while (dbus->message_iter_next(&array_iter)) {
            if (DBUS_TYPE_STRING == dbus->message_iter_get_arg_type(&array_iter)) {
                const char *str;
                dbus->message_iter_get_basic(&array_iter, &str);
                if(SDL_strcmp(str, SENSORFW_SENSOR_NAME_ACCELEROMETER) == 0) {
                    SDL_sensors[SDL_sensors_count].type = SDL_SENSOR_ACCEL;
                    SDL_sensors[SDL_sensors_count].instance_id = SDL_GetNextObjectID();
                    SDL_sensors[SDL_sensors_count].interface_name = SENSORFW_SENSOR_INTERFACE_ACCELEROMETER;
                    SDL_sensors[SDL_sensors_count].plugin_name = SENSORFW_SENSOR_NAME_ACCELEROMETER;
                    SDL_sensors[SDL_sensors_count].method_name = SENSORFW_SENSOR_METHOD_GET_GYROSCOPE;
                    //SDL_sscanf(SENSORFW_SENSOR_INTERFACE_ACCELEROMETER, "local.%s", SDL_sensors[SDL_sensors_count].name);
                    SDL_sensors[SDL_sensors_count].name = "Accelerometer Sensor";
                    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Added sensor #%d: %s", SDL_sensors_count, SDL_sensors[SDL_sensors_count].name);
                    SDL_sensors_count++;
                } else
                if(SDL_strcmp(str, SENSORFW_SENSOR_NAME_GYROSCOPE) == 0) {
                    SDL_sensors[SDL_sensors_count].type = SDL_SENSOR_GYRO;
                    SDL_sensors[SDL_sensors_count].instance_id = SDL_GetNextObjectID();
                    SDL_sensors[SDL_sensors_count].interface_name = SENSORFW_SENSOR_INTERFACE_GYROSCOPE;
                    SDL_sensors[SDL_sensors_count].method_name = SENSORFW_SENSOR_METHOD_GET_ORIENTATION;
                    SDL_sensors[SDL_sensors_count].plugin_name = SENSORFW_SENSOR_NAME_GYROSCOPE;
                    //SDL_sscanf(SENSORFW_SENSOR_NAME_GYROSCOPE, "local.%s", SDL_sensors[SDL_sensors_count].name);
                    SDL_sensors[SDL_sensors_count].name = "Gyroscope Sensor";
                    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Added sensor #%d: %s", SDL_sensors_count, SDL_sensors[SDL_sensors_count].name);
                    SDL_sensors_count++;
                } else {
                    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Plugin %s ignored.", str); 
                }
                count++;
            }
        }
        dbus->message_iter_next(&iter); // move to end

        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Plugin list yields %d/%d usable sensors.", SDL_sensors_count, count);
        SDL_DBus_FreeReply(&reply);
    } else {
        SDL_SetError("Could not list SensorFW plugins!");
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Could not list SensorFW plugins!");
        return false;
    }

    result = true;
#endif
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "SensorInit done.");
    return result;
}

static int SDL_SENSORFWDBUS_SensorGetCount(void)
{
    return SDL_sensors_count;
}

static void SDL_SENSORFWDBUS_SensorDetect(void)
{
}

static const char *SDL_SENSORFWDBUS_SensorGetDeviceName(int device_index)
{
    if (device_index < SDL_sensors_count) {
        return SDL_sensors[device_index].name;
    }
    return NULL;
}

static SDL_SensorType SDL_SENSORFWDBUS_SensorGetDeviceType(int device_index)
{
    if (device_index < SDL_sensors_count) {
        return SDL_sensors[device_index].type;
    }
    return SDL_SENSOR_INVALID;
}

static int SDL_SENSORFWDBUS_SensorGetDeviceNonPortableType(int device_index)
{
    /* TODO: enmerate this:
    if (device_index < SDL_sensors_count) {
        return SDL_sensors[device_index].ptype;
    }
    */
    return -1;
}

static SDL_SensorID SDL_SENSORFWDBUS_SensorGetDeviceInstanceID(int device_index)
{
    if (device_index < SDL_sensors_count) {
        return SDL_sensors[device_index].instance_id;
    }
    return -1;
}

static bool SDL_SENSORFWDBUS_SensorOpen(SDL_Sensor *sensor, int device_index)
{
    /*
    struct sensor_hwdata *hwdata;

    hwdata = (struct sensor_hwdata *)SDL_calloc(1, sizeof(*hwdata));
    if (!hwdata) {
        return false;
    }
    */
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return false;
    }

    // load the plugin:
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Loading plugin.");
    SDL_DBus_CallVoidMethodOnConnection(dbus->system_conn,
                                        SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                        SENSORFW_MANAGER_METHOD_LOAD_PLUGIN,
                                        DBUS_TYPE_STRING, &SDL_sensors[device_index].plugin_name,
                                        DBUS_TYPE_INVALID);


    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Requesting sensor session for %s", SDL_sensors[device_index].plugin_name);
    // request the sensor:
    DBusMessage *reply = NULL;
    int64_t pid = getpid();
    if(SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                        SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                        SENSORFW_MANAGER_METHOD_START_SESSION,
                                        DBUS_TYPE_STRING, &SDL_sensors[device_index].plugin_name,
                                        DBUS_TYPE_INT64, &pid,
                                        DBUS_TYPE_INVALID))
    {
#ifdef DEBUG_SENSORS
        SDL_sensors[device_index].plugin_loaded = true;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Ok");
    } else
    {
        char err[128];
        dbus->error_has_name(reply, &err);
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Failed: %s", err);
#endif
        return false;
    }
    DBusMessageIter iter;
    dbus->message_iter_init(reply, &iter);
//    dbus->message_iter_next(&iter);
    if (DBUS_TYPE_INT32 == dbus->message_iter_get_arg_type(&iter)) {
        int32_t id;
        dbus->message_iter_get_basic(&iter, &id);
        SDL_sensors[device_index].session = id;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Got Session Id %i", id);
    }
    SDL_DBus_FreeReply(&reply);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Starting sensor.");
    return SDL_DBus_CallVoidMethodOnConnection(dbus->system_conn,
                                        SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT,
                                        SDL_sensors[device_index].interface_name,
                                        SENSORFW_SENSOR_METHOD_START,
                                        DBUS_TYPE_INT32, &SDL_sensors[device_index].session,
                                        DBUS_TYPE_INVALID);


#endif
    //sensor->hwdata = hwdata;
    return false;
}

static void SDL_SENSORFWDBUS_SensorUpdate(SDL_Sensor *sensor)
{
//   SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Update");

//   switch (sensor->type) {
//   case SDL_SENSOR_ACCEL:
//       SensorFW_UpdateAccelDbus(sensor);
//       break;
//   case SDL_SENSOR_GYRO:
//       SensorFW_UpdateGyroDbus(sensor);
//       break;
//   default:
//       break;
//   }
}
static void SDL_SENSORFWDBUS_SensorClose(SDL_Sensor *sensor)
{
}

static void SDL_SENSORFWDBUS_SensorQuit(void)
{
}

SDL_SensorDriver SDL_SENSORFWDBUS_SensorDriver = {
    SDL_SENSORFWDBUS_SensorInit,
    SDL_SENSORFWDBUS_SensorGetCount,
    SDL_SENSORFWDBUS_SensorDetect,
    SDL_SENSORFWDBUS_SensorGetDeviceName,
    SDL_SENSORFWDBUS_SensorGetDeviceType,
    SDL_SENSORFWDBUS_SensorGetDeviceNonPortableType,
    SDL_SENSORFWDBUS_SensorGetDeviceInstanceID,
    SDL_SENSORFWDBUS_SensorOpen,
    SDL_SENSORFWDBUS_SensorUpdate,
    SDL_SENSORFWDBUS_SensorClose,
    SDL_SENSORFWDBUS_SensorQuit,
};

static void SensorFW_UpdateAccelDbus(SDL_Sensor *sensor)
{
    //SfwSensor *ss = SDL_sensors[0].sensor; // 0 is Accel

//    static SfwSampleXyz previous_state = { 0, 0, 0, 0 };
    float data[3];
    Uint64 timestamp = SDL_GetTicksNS();
/*
    SfwReading* r = sfwsensor_reading(ss);
    const SfwSampleAccelerometer* smpl = sfwreading_accelerometer(r);
    SfwSampleXyz current_state = { 0, smpl->x, smpl->y, smpl->z };

    if (SDL_memcmp(&previous_state, &current_state, sizeof(SfwSampleXyz)) != 0) {
        SDL_memcpy(&previous_state, &current_state, sizeof(SfwSampleXyz));
        data[0] = current_state.x * SDL_STANDARD_GRAVITY;
        data[1] = current_state.y * SDL_STANDARD_GRAVITY;
        data[2] = current_state.z * SDL_STANDARD_GRAVITY;
        SDL_SendSensorUpdate(timestamp, sensor, smpl->timestamp, data, sizeof(data));
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Sent Accel reading: %s", sfwreading_repr(r));
    }
*/
}
static void SensorFW_UpdateGyroDbus(SDL_Sensor *sensor)
{
    //SfwSensor *ss = SDL_sensors[1].sensor; // 1 is Gyro

//    static SfwSampleXyz previous_state = { 0, 0, 0, 0 };
    float data[3];
    Uint64 timestamp = SDL_GetTicksNS();

/*
    SfwReading* r = sfwsensor_reading(ss);
    const SfwSampleGyroscope* smpl = sfwreading_gyroscope(r);
    SfwSampleXyz current_state = { 0, smpl->x, smpl->y, smpl->z };


    if (SDL_memcmp(&previous_state, &current_state, sizeof(SfwSampleXyz)) != 0) {
        SDL_memcpy(&previous_state, &current_state, sizeof(SfwSampleXyz));
        data[0] = current_state.x;
        data[1] = current_state.y;
        data[2] = current_state.z;
        SDL_SendSensorUpdate(timestamp, sensor, smpl->timestamp, data, sizeof(data));
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Sent Gyro reading: %s", sfwreading_repr(r));
    }
*/
}

#endif // SDL_SENSOR_SENSORFW
