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

// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  getAvailableIntervals   "a(ddd)"
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  getAvailableBufferSizes "a(uu)"
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  getAvailableDataRanges  "a(ddd)"

// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  id
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  start
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  stop

// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  setStandbyOverride
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  setDataRate
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  setBufferInterval
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  setBufferSize
// com.nokia.SensorService  /SensorManager/gyroscopesensor  local.GyroscopeSensor  setDownsampling


typedef struct
{
    SDL_SensorType type;
    const char*    name;
    SDL_SensorID   instance_id;

    const char*    plugin_name;
    const char*    interface_name;
    const char*    path;
    const char*    method_name;
    int32_t        session;
} SDL_SensorFWSensor;

static SDL_SensorFWSensor *SDL_sensors;
static int SDL_sensors_count;

static int64_t pid;

static void _SetDataRate(const SDL_SensorFWSensor* sensor, const double rate);

static bool SDL_SENSORFWDBUS_SensorInit(void)
{
#ifndef SDL_USE_LIBDBUS
    return SDL_Unsupported();
#endif
    bool result = false;
    SDL_sensors_count = 0;
    pid = getpid();

#ifdef SDL_USE_LIBDBUS

    // TODO: we alloc exactly two sensors.
    // SensorFW supports more, but SDL (basically)only supports accel and gyro.
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
        do {
            if (DBUS_TYPE_STRING == dbus->message_iter_get_arg_type(&array_iter)) {
                const char *str;
                char* path;
                dbus->message_iter_get_basic(&array_iter, &str);
                if(SDL_strcmp(str, SENSORFW_SENSOR_NAME_ACCELEROMETER) == 0) {
                    SDL_asprintf(&path, "%s/%s", SENSORFW_MANAGER_OBJECT, SENSORFW_SENSOR_NAME_ACCELEROMETER);
                    SDL_sensors[SDL_sensors_count].type = SDL_SENSOR_ACCEL;
                    SDL_sensors[SDL_sensors_count].instance_id = SDL_GetNextObjectID();
                    SDL_sensors[SDL_sensors_count].interface_name = SENSORFW_SENSOR_INTERFACE_ACCELEROMETER;
                    SDL_sensors[SDL_sensors_count].plugin_name = SENSORFW_SENSOR_NAME_ACCELEROMETER;
                    SDL_sensors[SDL_sensors_count].method_name = SENSORFW_SENSOR_METHOD_GET_ACCELEROMETER;
                    SDL_sensors[SDL_sensors_count].path = (const char*) SDL_strdup(path);
                    SDL_sensors[SDL_sensors_count].name = "Accelerometer Sensor";
                    SDL_sensors_count++;
                } else
                if(SDL_strcmp(str, SENSORFW_SENSOR_NAME_GYROSCOPE) == 0) {
                    SDL_asprintf(&path, "%s/%s", SENSORFW_MANAGER_OBJECT, SENSORFW_SENSOR_NAME_GYROSCOPE);
                    SDL_sensors[SDL_sensors_count].type = SDL_SENSOR_GYRO;
                    SDL_sensors[SDL_sensors_count].instance_id = SDL_GetNextObjectID();
                    SDL_sensors[SDL_sensors_count].interface_name = SENSORFW_SENSOR_INTERFACE_GYROSCOPE;
                    SDL_sensors[SDL_sensors_count].plugin_name = SENSORFW_SENSOR_NAME_GYROSCOPE;
                    SDL_sensors[SDL_sensors_count].method_name = SENSORFW_SENSOR_METHOD_GET_GYROSCOPE;
                    SDL_sensors[SDL_sensors_count].path = (const char*) SDL_strdup(path);
                    SDL_sensors[SDL_sensors_count].name = "Gyroscope Sensor";
                    SDL_sensors_count++;
#ifdef DEBUG_SENSORS
                } else {
                    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Plugin %s ignored.", str); 
#endif
                }
                count++;
            }
        } while (dbus->message_iter_next(&array_iter));
        dbus->message_iter_next(&iter); // move to end

        SDL_DBus_FreeReply(&reply);
        result = true;
#ifdef DEBUG_SENSORS
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Plugin list yields %d/%d usable sensors.", SDL_sensors_count, count);
#endif
    } else {
        result = false;
    }

#endif
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
    return (int)SDL_SENSORFWDBUS_SensorGetDeviceType(device_index);
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
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return false;
    }
    SDL_SensorFWSensor fwsensor = SDL_sensors[device_index];

#ifdef DEBUG_SENSORS
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Requesting sensor session for %s", fwsensor.plugin_name);
#endif
    // request the sensor:
    DBusMessage *reply = NULL;
    SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                    SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                    SENSORFW_MANAGER_METHOD_START_SESSION,
                                    DBUS_TYPE_STRING, &fwsensor.plugin_name,
                                    DBUS_TYPE_INT64, &pid,
                                    DBUS_TYPE_INVALID);
    if (reply == NULL) {
        SDL_SetError("Could not request sensor: %s", SDL_GetError());
        return false;
    }
    DBusMessageIter iter;
    dbus->message_iter_init(reply, &iter);
    if (DBUS_TYPE_INT32 == dbus->message_iter_get_arg_type(&iter)) {
        int32_t id;
        dbus->message_iter_get_basic(&iter, &id);
        fwsensor.session = id;
    }
    SDL_DBus_FreeReply(&reply);

    _SetDataRate(&fwsensor, 60.0f);
    // start sensor instance
    return SDL_DBus_CallVoidMethodOnConnection(dbus->system_conn,
                                        SENSORFW_SERVICE,
                                        fwsensor.path,
                                        fwsensor.interface_name,
                                        SENSORFW_SENSOR_METHOD_START,
                                        DBUS_TYPE_INT32, &fwsensor.session,
                                        DBUS_TYPE_INVALID);

#endif
    return false;
}

static void SDL_SENSORFWDBUS_SensorUpdate(SDL_Sensor *sensor)
{
#ifdef SDL_USE_LIBDBUS
    Uint64 timestamp = SDL_GetTicksNS();

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return;
    }

    for (int index = 0; index < SDL_sensors_count; ++index) {
        SDL_SensorFWSensor fwsensor = SDL_sensors[index];
        DBusMessage *reply = NULL;
        if (!SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                             SENSORFW_SERVICE,
                                             fwsensor.path,
                                             fwsensor.interface_name,
                                             fwsensor.method_name,
                                             DBUS_TYPE_INVALID))
        {
            if (reply != NULL) {
                DBusMessageIter iter;
                DBusMessageIter struct_iter;
                uint64_t ts = 0;
                uint32_t value = 0;
                float data[3];
                int data_index = 0;
                /* "value" call has 
                STRUCT "tddd" {
                        UINT64 445538660099;
                        DOUBLE -1093.72;
                        DOUBLE -199041;
                        DOUBLE 78122.8;
                };
                */
                dbus->message_iter_init(reply, &iter);
                if (DBUS_TYPE_STRUCT == dbus->message_iter_get_arg_type(&iter)) {
                    dbus->message_iter_recurse(&iter, &struct_iter);
                    do {
                        if (DBUS_TYPE_UINT64 == dbus->message_iter_get_arg_type(&struct_iter)) {
                            dbus->message_iter_get_basic(&struct_iter, &ts);
                        } else
                        if (DBUS_TYPE_UINT32 == dbus->message_iter_get_arg_type(&struct_iter)) {
                            dbus->message_iter_get_basic(&struct_iter, (float*)&value);
                        } else
                        if (DBUS_TYPE_DOUBLE == dbus->message_iter_get_arg_type(&struct_iter)) {
                            dbus->message_iter_get_basic(&struct_iter, &data[data_index]);
                            data_index++;
                        }
#ifdef DEBUG_SENSORS
                        else {
                            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Unknown value: %d", dbus->message_iter_get_arg_type(&struct_iter));
#endif
                    } while (dbus->message_iter_next(&struct_iter));
                }
                SDL_DBus_FreeReply(&reply);
                if (data_index > 0) {
                    SDL_SendSensorUpdate(timestamp, sensor, ts, data, sizeof(data));
                    return;
                } else {
                  // FIXME: should type-pun, not cast:
                    SDL_SendSensorUpdate(timestamp, sensor, ts, (float*) &value, sizeof((float)value));
                    return;
                }
            }
        }
    }
#endif
}

static void SDL_SENSORFWDBUS_SensorClose(SDL_Sensor *sensor)
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    if (!dbus || !dbus->system_conn) {
        return;
    }

    for (int index = 0; index < SDL_sensors_count; ++index) {
        SDL_DBus_CallMethodOnConnection(dbus->system_conn, NULL,
                                        SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                        SENSORFW_MANAGER_METHOD_STOP_SESSION,
                                        DBUS_TYPE_STRING, &SDL_sensors[index].plugin_name,
                                        DBUS_TYPE_INT32, &SDL_sensors[index].session,
                                        DBUS_TYPE_INT64, &pid,
                                        DBUS_TYPE_INVALID);
    }
#endif
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

static void _SetDataRate(const SDL_SensorFWSensor* sensor, const double rate)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    SDL_DBus_CallMethodOnConnection(dbus->system_conn, NULL,
                                    SENSORFW_SERVICE,
                                    sensor->path,
                                    sensor->interface_name,
                                    SENSORFW_SENSOR_METHOD_SET_DATARATE,
                                    DBUS_TYPE_INT32, &sensor->session,
                                    DBUS_TYPE_DOUBLE, &rate,
                                    DBUS_TYPE_INVALID);
}

/*
static void _GetSensorProperties(const SDL_SensorFWSensor* sensor, void* user_data)
{
    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    DBusMessage *reply = NULL;
    SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                           SENSORFW_SERVICE,
                           sensor->path,
                           "org.freedesktop.DBus.Properties",
                           "GetAll",
                           DBUS_TYPE_STRING, sensor->interface_name,
                           DBUS_TYPE_INVALID);
    SDL_DBus_FreeReply(&reply);
}
*/

#endif // SDL_SENSOR_SENSORFW
