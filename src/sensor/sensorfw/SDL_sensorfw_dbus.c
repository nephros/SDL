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

/* For the SensorFW DBus API, the best "documentation" I have found is at
 *    https://github.com/sailfishos/sensorfw/blob/master/doc/mainpage.h
 *
 * The manager can be introspected like so:
 *
 * busctl introspect "com.nokia.SensorService"  "/SensorManager"
 *
 * The rest of the interface is only available after calling loadPlugin or requestSensor.
 *
 * Basic sequence of events:
 *  - list "plugins" via "availableSensorPlugins"
 *  - request a sensor using "requestSensor(sx)" (s=sensorpluginname x=PID)  -> returns a session id
 *    This session id is given as parameter to any method calls on the sensor's bus
 *  - now at least the new interfaces/object paths are available
 *       e.g. com.nokia.SensorService /SensorManager/accelerometersensor  local.Accelerometer
 *  - set up paramers (data rate etc)
 *  - call the "start" method on the sensor bus
 *  - now we can either wait for the "dataAvailable" signal, or call the
 *    reading method (e.g. "xyz()") for the sensor
 *  - different sensors have different reading methods, and those may return various signatures
 *    Luckily, both Accelerometer and Gyroscope return "(tiii)".
 */

/* values copied straight from
 * https://github.com/sailfishos/sensors-glib/blob/main/sfwdbus.h
 *
 * TODO: shouldn't we make some these into a struct/union/enum?
 */

#define SENSORFW_SERVICE                          "com.nokia.SensorService"

#define SENSORFW_MANAGER_OBJECT                   "/SensorManager"
#define SENSORFW_MANAGER_IFACE                    "local.SensorManager"
#define SENSORFW_MANAGER_METHOD_LOAD_PLUGIN       "loadPlugin"
#define SENSORFW_MANAGER_METHOD_START_SESSION     "requestSensor"
#define SENSORFW_MANAGER_METHOD_STOP_SESSION      "releaseSensor"
#define SENSORFW_MANAGER_METHOD_AVAILABLE_PLUGINS "availableSensorPlugins"

#define SENSORFW_SENSOR_SIGNAL_DATA_AVAILABLE     "dataAvailable"

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
    int            ptype;
    const char*    name;
    SDL_SensorID   instance_id;

    const char*    plugin_name;
    const char*    interface_name;
    const char*    path;
    const char*    method_name;
    int32_t        session;

    uint32_t       interval;
} SFWSensor;

static SFWSensor *SDL_sensors;
static int SDL_sensors_count;

static int64_t pid;

static bool _getSensorStringProperty(const SFWSensor* sensor, const char* property, char* result);
static bool _getSensorIntProperty(const SFWSensor* sensor, const char* property, int32_t result );
static bool _getSensorUIntProperty(const SFWSensor* sensor, const char* property, uint32_t result);
static bool _getSensorBoolProperty(const SFWSensor* sensor, const char* property, bool result);
static void _setSensorDataRate(const SFWSensor* sensor, const double rate);

static void DBus_handleReadingTIII(SDL_Sensor *sensor, DBusMessage *reply);

static DBusHandlerResult DBus_MessageFilter(DBusConnection *conn, DBusMessage *msg, void *data);

static bool SDL_SENSORFWDBUS_SensorInit(void)
{
#ifndef SDL_USE_LIBDBUS
    return SDL_Unsupported();
#else
    bool result = false;
    SDL_sensors_count = 0;
    pid = getpid();

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    /* we check for this here, and never again... */
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
        // iterate once to get the count:
        int count = 0;
        DBusMessageIter iter;
        DBusMessageIter array_iter;
        dbus->message_iter_init(reply, &iter);
        dbus->message_iter_recurse(&iter, &array_iter);
        do {
            count++;
        } while (dbus->message_iter_next(&array_iter));

        SDL_sensors = (SFWSensor *)SDL_calloc(count, sizeof(*SDL_sensors));
        if (!SDL_sensors) {
            SDL_DBus_FreeReply(&reply);
            return false;
        }

        // add the actual sensors:
        dbus->message_iter_init(reply, &iter);
        dbus->message_iter_recurse(&iter, &array_iter);
        // Used to make a "unique" platform ID
        count = -2; // yes, two. -1 means "sensor is NULL", see SDL_GetSensorNonPortableType
        do {
            if (DBUS_TYPE_STRING == dbus->message_iter_get_arg_type(&array_iter)) {
                const char *str;
                char* path;
                dbus->message_iter_get_basic(&array_iter, &str);
                SFWSensor* new_s = &SDL_sensors[SDL_sensors_count];
                new_s->instance_id = SDL_GetNextObjectID();
                if(SDL_strcmp(SENSORFW_SENSOR_NAME_ACCELEROMETER, str) == 0) {

                    new_s->type = SDL_SENSOR_ACCEL;
                    new_s->name = "Accelerometer Sensor";

                    new_s->interface_name = SENSORFW_SENSOR_INTERFACE_ACCELEROMETER;
                    new_s->plugin_name = SENSORFW_SENSOR_NAME_ACCELEROMETER;
                    new_s->method_name = SENSORFW_SENSOR_METHOD_GET_ACCELEROMETER;

                    SDL_asprintf(&path, "%s/%s", SENSORFW_MANAGER_OBJECT, SENSORFW_SENSOR_NAME_ACCELEROMETER);
                    new_s->path = (const char*) SDL_strdup(path);
                    SDL_free(path);
                }
                else if(SDL_strcmp(SENSORFW_SENSOR_NAME_GYROSCOPE, str) == 0) {

                    new_s->type = SDL_SENSOR_GYRO;
                    new_s->name = "Gyroscope Sensor";

                    new_s->interface_name = SENSORFW_SENSOR_INTERFACE_GYROSCOPE;
                    new_s->plugin_name = SENSORFW_SENSOR_NAME_GYROSCOPE;
                    new_s->method_name = SENSORFW_SENSOR_METHOD_GET_GYROSCOPE;

                    SDL_asprintf(&path, "%s/%s", SENSORFW_MANAGER_OBJECT, SENSORFW_SENSOR_NAME_GYROSCOPE);
                    new_s->path = (const char*) SDL_strdup(path);
                    SDL_free(path);
                }
                else {
                    // FIXME: we could actually determine the dbus stuff here as well.
                    new_s->type = SDL_SENSOR_UNKNOWN;
                    new_s->ptype = count;
                    new_s->plugin_name = str;

                    // Construct a prettier name:
                    char* name = SDL_strdup("");
                    const char* end = SDL_strstr(str, "sensor");
                    SDL_strlcpy(name, str, SDL_strlen(str)-SDL_strlen(end)+1);
                    name[0] = SDL_toupper(name[0]);
                    SDL_strlcat(name, " Platform Sensor", SDL_strlen(name) + 17);
                    new_s->name = (const char*) SDL_strdup(name);
                    SDL_free(name);
                }
                SDL_sensors_count++;
                count--;
            }
        } while (dbus->message_iter_next(&array_iter));
        dbus->message_iter_next(&iter); // move to end

        SDL_DBus_FreeReply(&reply);
        result = true;
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
    if (device_index < SDL_sensors_count) {
        return SDL_sensors[device_index].ptype;
    }
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
    SFWSensor* sfwsensor = &SDL_sensors[device_index];

    // TODO
    if ((sensor->type == SDL_SENSOR_UNKNOWN)
        && (-1 > SDL_SENSORFWDBUS_SensorGetDeviceNonPortableType(device_index))) {
        SDL_SetError("not implemented.");
        return false;
    }

#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

#ifdef DEBUG_SENSORS
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Requesting sensor session for %s", sfwsensor->plugin_name);
#endif
    // request the sensor:
    DBusMessage *reply = NULL;
    SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                    SENSORFW_SERVICE, SENSORFW_MANAGER_OBJECT, SENSORFW_MANAGER_IFACE,
                                    SENSORFW_MANAGER_METHOD_START_SESSION,
                                    DBUS_TYPE_STRING, &sfwsensor->plugin_name,
                                    DBUS_TYPE_INT64, &pid,
                                    DBUS_TYPE_INVALID);
    if (reply == NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Could not request sensor: %s", SDL_GetError());
        SDL_SetError("Could not request sensor: %s", SDL_GetError());
        return false;
    }
    DBusMessageIter iter;
    dbus->message_iter_init(reply, &iter);
    if (DBUS_TYPE_INT32 == dbus->message_iter_get_arg_type(&iter)) {
        int32_t id;
        dbus->message_iter_get_basic(&iter, &id);
        sfwsensor->session = id;
    }
    SDL_DBus_FreeReply(&reply);

    char* desc = NULL;
    if(_getSensorStringProperty(sfwsensor, "description", desc)) {
        if (desc != NULL) {
            SDL_Log("Sensor %d: %s: %s", device_index, SDL_SENSORFWDBUS_SensorGetDeviceName(device_index), desc);
        } else
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Sensor %d has no description", device_index);
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Error getting description for Sensor %d", device_index);
    }

    // start sensor instance
    bool ok = SDL_DBus_CallVoidMethodOnConnection(dbus->system_conn,
                                        SENSORFW_SERVICE,
                                        sfwsensor->path,
                                        sfwsensor->interface_name,
                                        SENSORFW_SENSOR_METHOD_START,
                                        DBUS_TYPE_INT32, &sfwsensor->session,
                                        DBUS_TYPE_INVALID);

    // determine and store the data rate/interval:
    _getSensorUIntProperty(sfwsensor, "interval", sfwsensor->interval);

    //_setDataRate(&sfwsensor, 60.0f);
#ifdef DEBUG_SENSORS
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Interval is %lums", sfwsensor->interval);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Setting up handler for %s", sfwsensor->plugin_name);
#endif

    char* matchstring = SDL_strdup("");
    SDL_asprintf(&matchstring, "type='signal', interface='%s', member='%s'",
                 sfwsensor->interface_name,
                 SENSORFW_SENSOR_SIGNAL_DATA_AVAILABLE);

    dbus->bus_add_match(dbus->system_conn, SDL_strdup(matchstring), NULL);
    dbus->connection_add_filter(dbus->system_conn,
                                &DBus_MessageFilter, sensor, NULL);
    dbus->connection_flush(dbus->system_conn);
    SDL_free(matchstring);

    return ok;
#endif
    return false;
}

static void SDL_SENSORFWDBUS_SensorUpdate(SDL_Sensor *sensor)
{
    // TODO
    if (sensor->type == SDL_SENSOR_UNKNOWN) {
        SDL_SetError("not implemented.");
        return;
    }
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    for (int index = 0; index < SDL_sensors_count; index++) {
        SFWSensor* sfwsensor = &SDL_sensors[index];
        if(sensor->instance_id != sfwsensor->instance_id) continue;

        DBusMessage *reply = NULL;
        SDL_DBus_CallMethodOnConnection(dbus->system_conn, &reply,
                                             SENSORFW_SERVICE,
                                             sfwsensor->path,
                                             sfwsensor->interface_name,
                                             sfwsensor->method_name,
                                             DBUS_TYPE_INVALID);
        if (reply != NULL) {
            // parse and send the sensorvalues
            DBus_handleReadingTIII(sensor, reply);
            SDL_DBus_FreeReply(&reply);
            // wait for the next update:
            if(sfwsensor->interval > 0) SDL_DelayNS(sfwsensor->interval);
        }
    }
#endif
}

static void SDL_SENSORFWDBUS_SensorClose(SDL_Sensor *sensor)
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

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

static void _setSensorDataRate(const SFWSensor* sensor, const double rate)
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

static bool _getSensorStringProperty(const SFWSensor* sensor, const char* property, char* result )
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    DBusMessage* reply;
    SDL_zero(reply);
    // save_reply must be non-NULL if it's a string property
    bool ok = SDL_DBus_QueryPropertyOnConnection(dbus->system_conn, &reply,
                                       SENSORFW_SERVICE,
                                       sensor->path,
                                       sensor->interface_name,
                                       property,
                                       DBUS_TYPE_STRING,
                                       &result);
    if (reply != NULL) {
        return true;
    }
    SDL_DBus_FreeReply(&reply);
#endif
    return ok;
}

static bool _getSensorIntProperty(const SFWSensor* sensor, const char* property, int32_t result)
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    return SDL_DBus_QueryPropertyOnConnection(dbus->system_conn, NULL,
                                              SENSORFW_SERVICE,
                                              sensor->path,
                                              sensor->interface_name,
                                              property,
                                              DBUS_TYPE_INT32,
                                              &result);
#endif
    return false;
}

static bool _getSensorUIntProperty(const SFWSensor* sensor, const char* property, uint32_t result)
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    return SDL_DBus_QueryPropertyOnConnection(dbus->system_conn, NULL,
                                              SENSORFW_SERVICE,
                                              sensor->path,
                                              sensor->interface_name,
                                              property,
                                              DBUS_TYPE_UINT32,
                                              &result);
#endif
    return false;
}

static bool _getSensorBoolProperty(const SFWSensor* sensor, const char* property, bool result)
{
#ifdef SDL_USE_LIBDBUS

    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    return SDL_DBus_QueryPropertyOnConnection(dbus->system_conn, NULL,
                                              SENSORFW_SERVICE,
                                              sensor->path,
                                              sensor->interface_name,
                                              property,
                                              DBUS_TYPE_BOOLEAN,
                                              &result);
#endif
    return false;
}


static DBusHandlerResult DBus_MessageFilter(DBusConnection *conn, DBusMessage *msg, void *data)
{
//#ifdef DEBUG_SENSORS
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Callback: MessageFilter");
//#endif
#ifdef SDL_USE_LIBDBUS
    SDL_DBusContext *dbus = SDL_DBus_GetContext();
    SDL_Sensor* sensor = (SDL_Sensor*)data;

    if ((dbus->message_is_signal(msg, SENSORFW_SENSOR_INTERFACE_GYROSCOPE, SENSORFW_SENSOR_SIGNAL_DATA_AVAILABLE))
     || (dbus->message_is_signal(msg, SENSORFW_SENSOR_INTERFACE_ACCELEROMETER, SENSORFW_SENSOR_SIGNAL_DATA_AVAILABLE))) {
        DBus_handleReadingTIII(sensor, msg);
        return DBUS_HANDLER_RESULT_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
#endif
}

// FIXME: this only deals with accel and gyro readings, and badly.
//        add proper parsing!
static void DBus_handleReadingTIII(SDL_Sensor *sensor, DBusMessage *reply)
{
#ifdef SDL_USE_LIBDBUS
    SDL_DBusContext *dbus = SDL_DBus_GetContext();

    DBusMessageIter iter;
    DBusMessageIter struct_iter;
    Uint64 timestamp = SDL_GetTicksNS();
    uint64_t sensorstamp = 0;
    float data[3] = { 0, 0, 0};
    int data_index = 0;
    // for a list, see https://github.com/sailfishos/sensorfw/blob/master/doc/mainpage.h
    // according to that, both "value" and "xyz" return "(tiii)" (struct)
    dbus->message_iter_init(reply, &iter);
    if (DBUS_TYPE_STRUCT == dbus->message_iter_get_arg_type(&iter)) {
        dbus->message_iter_recurse(&iter, &struct_iter);
        do {
            if (DBUS_TYPE_UINT64 == dbus->message_iter_get_arg_type(&struct_iter)) {
                // assuming ms
                dbus->message_iter_get_basic(&struct_iter, &sensorstamp);
            } else
            /*
            if (DBUS_TYPE_UINT32 == dbus->message_iter_get_arg_type(&struct_iter)) {
                uint32_t tmp;
                dbus->message_iter_get_basic(&struct_iter, &tmp);
                value = (float) tmp;
            } else
            */
            if (DBUS_TYPE_INT32 == dbus->message_iter_get_arg_type(&struct_iter)) {
                int32_t tmp;
                dbus->message_iter_get_basic(&struct_iter, &tmp);
                data[data_index] = tmp/1.0f; //convert to float
                data_index++;
            /*
            } else
            if (DBUS_TYPE_DOUBLE == dbus->message_iter_get_arg_type(&struct_iter)) {
                dbus->message_iter_get_basic(&struct_iter, &data[data_index]);
                data_index++;
            */
            }
#ifdef DEBUG_SENSORS
            else {
                SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Unknown value: %d", dbus->message_iter_get_arg_type(&struct_iter));
#endif
        } while (dbus->message_iter_next(&struct_iter));
   }
   // relatively stupid avoidance of empty readings:
   const float checkdata[3] = { 0, 0, 0 };
   if (SDL_memcmp(&checkdata, &data, sizeof(data)) != 0) //data is not null
       SDL_SendSensorUpdate(timestamp, sensor, sensorstamp, data, sizeof(data));
#endif
}

#endif // SDL_SENSOR_SENSORFW
