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

#include <glib-2.0/glib.h>
#include <sensors-glib/sfwreporting.h>
#include <sensors-glib/sfwsensor.h>
#include <sensors-glib/sfwplugin.h>

typedef struct
{
     SDL_SensorType type;
     SfwSensorId ptype;
     SDL_SensorID instance_id;
     SfwSensor *sensor;
     unsigned long handlerId;
} SDL_SensorFWSensor;

struct sensor_hwdata
{
    bool active;
    bool valid;
//    SfwPlugin  *plugin;
//    SfwService *service;
    const char *name;
    const char *object;
    const char *interface;
};

static SDL_SensorFWSensor *SDL_sensors;
static int SDL_sensors_count;

const double datarate_hz = 60;

static void SensorFW_UpdateAccel(SDL_Sensor *sensor);
static void SensorFW_UpdateGyro(SDL_Sensor *sensor);

#ifdef DEBUG_SENSORS
static void activeChangedCB(SfwSensor *sfwsensor, void* aptr);
static void validChangedCB(SfwSensor *sfwsensor, void* aptr);
#endif
static void accelUpdateCB(SfwSensor *sfwsensor, void* aptr);
static void gyroUpdateCB(SfwSensor *sfwsensor, void* aptr);

static GMainContext *maincontext;

static bool SDL_SENSORFW_SensorInit(void)
{

    SDL_sensors_count = 14;

    sfwsensor_new(SFW_SENSOR_ID_ACCELEROMETER);
    sfwsensor_new(SFW_SENSOR_ID_GYROSCOPE);

    SDL_sensors = (SDL_SensorFWSensor *)SDL_calloc(SDL_sensors_count, sizeof(*SDL_sensors));
    if (!SDL_sensors) {
        return false;
    }

    SDL_sensors[0].sensor = sfwsensor_new(SFW_SENSOR_ID_ACCELEROMETER);
    SDL_sensors[0].ptype = SFW_SENSOR_ID_ACCELEROMETER;
    SDL_sensors[0].type = SDL_SENSOR_ACCEL;
    SDL_sensors[0].instance_id = SDL_GetNextObjectID();

    SDL_sensors[1].sensor = sfwsensor_new(SFW_SENSOR_ID_GYROSCOPE);
    SDL_sensors[1].ptype = SFW_SENSOR_ID_GYROSCOPE;
    SDL_sensors[1].type = SDL_SENSOR_GYRO;
    SDL_sensors[1].instance_id = SDL_GetNextObjectID();

    // These are in sensorfw, but not in SDL:
    SDL_sensors[2].sensor = sfwsensor_new(SFW_SENSOR_ID_PROXIMITY);
    SDL_sensors[2].ptype = SFW_SENSOR_ID_PROXIMITY;
    SDL_sensors[2].type = SDL_SENSOR_INVALID;
    SDL_sensors[2].instance_id = SDL_GetNextObjectID();
    SDL_sensors[3].sensor = sfwsensor_new(SFW_SENSOR_ID_ALS);
    SDL_sensors[3].ptype = SFW_SENSOR_ID_ALS;
    SDL_sensors[3].type = SDL_SENSOR_INVALID;
    SDL_sensors[3].instance_id = SDL_GetNextObjectID();
    SDL_sensors[4].sensor = sfwsensor_new(SFW_SENSOR_ID_ORIENTATION);
    SDL_sensors[4].ptype = SFW_SENSOR_ID_ORIENTATION;
    SDL_sensors[4].type = SDL_SENSOR_INVALID;
    SDL_sensors[4].instance_id = SDL_GetNextObjectID();
    //SFW_SENSOR_ID_ACCELEROMETER,
    SDL_sensors[5].sensor = sfwsensor_new(SFW_SENSOR_ID_COMPASS);
    SDL_sensors[5].ptype = SFW_SENSOR_ID_COMPASS;
    SDL_sensors[5].type = SDL_SENSOR_INVALID;
    SDL_sensors[5].instance_id = SDL_GetNextObjectID();
    //SFW_SENSOR_ID_GYROSCOPE,
    SDL_sensors[6].sensor = sfwsensor_new(SFW_SENSOR_ID_LID);
    SDL_sensors[6].ptype = SFW_SENSOR_ID_LID;
    SDL_sensors[6].type = SDL_SENSOR_INVALID;
    SDL_sensors[6].instance_id = SDL_GetNextObjectID();
    SDL_sensors[7].sensor = sfwsensor_new(SFW_SENSOR_ID_HUMIDITY);
    SDL_sensors[7].ptype = SFW_SENSOR_ID_HUMIDITY;
    SDL_sensors[7].type = SDL_SENSOR_INVALID;
    SDL_sensors[7].instance_id = SDL_GetNextObjectID();
    SDL_sensors[8].sensor = sfwsensor_new(SFW_SENSOR_ID_MAGNETOMETER);
    SDL_sensors[8].ptype = SFW_SENSOR_ID_MAGNETOMETER;
    SDL_sensors[8].type = SDL_SENSOR_INVALID;
    SDL_sensors[8].instance_id = SDL_GetNextObjectID();
    SDL_sensors[9].sensor = sfwsensor_new(SFW_SENSOR_ID_PRESSURE);
    SDL_sensors[9].ptype = SFW_SENSOR_ID_PRESSURE;
    SDL_sensors[9].type = SDL_SENSOR_INVALID;
    SDL_sensors[9].instance_id = SDL_GetNextObjectID();
    SDL_sensors[10].sensor = sfwsensor_new(SFW_SENSOR_ID_ROTATION);
    SDL_sensors[10].ptype =SFW_SENSOR_ID_ROTATION;
    SDL_sensors[10].type = SDL_SENSOR_INVALID;
    SDL_sensors[10].instance_id = SDL_GetNextObjectID();
    SDL_sensors[11].sensor = sfwsensor_new(SFW_SENSOR_ID_STEPCOUNTER);
    SDL_sensors[11].ptype = SFW_SENSOR_ID_STEPCOUNTER;
    SDL_sensors[11].type = SDL_SENSOR_INVALID;
    SDL_sensors[11].instance_id = SDL_GetNextObjectID();
    SDL_sensors[12].sensor = sfwsensor_new(SFW_SENSOR_ID_TAP);
    SDL_sensors[12].ptype = SFW_SENSOR_ID_TAP;
    SDL_sensors[12].type = SDL_SENSOR_INVALID;
    SDL_sensors[12].instance_id = SDL_GetNextObjectID();
    SDL_sensors[13].sensor = sfwsensor_new(SFW_SENSOR_ID_TEMPERATURE);
    SDL_sensors[13].ptype = SFW_SENSOR_ID_TEMPERATURE;
    SDL_sensors[13].type = SDL_SENSOR_INVALID;
    SDL_sensors[13].instance_id = SDL_GetNextObjectID();

    maincontext = g_main_context_new();
    return true;
}

static int SDL_SENSORFW_SensorGetCount(void)
{
    return SDL_sensors_count;
}

static void SDL_SENSORFW_SensorDetect(void)
{
}

static const char *SDL_SENSORFW_SensorGetDeviceName(int device_index)
{
    //return sfwsensor_name(SDL_sensors[device_index].sensor);
    return sfwsensorid_name(SDL_sensors[device_index].ptype);
    //return NULL;
}

static SDL_SensorType SDL_SENSORFW_SensorGetDeviceType(int device_index)
{
    return SDL_sensors[device_index].type;
}

static int SDL_SENSORFW_SensorGetDeviceNonPortableType(int device_index)
{
    return SDL_sensors[device_index].ptype;
}

static SDL_SensorID SDL_SENSORFW_SensorGetDeviceInstanceID(int device_index)
{
    return SDL_sensors[device_index].instance_id;
}

static bool SDL_SENSORFW_SensorOpen(SDL_Sensor *sensor, int device_index)
{
   SfwSensor* sfwsensor = SDL_sensors[device_index].sensor;
   struct sensor_hwdata *hwdata;

   hwdata = (struct sensor_hwdata *)SDL_calloc(1, sizeof(*hwdata));
   if (!hwdata) {
       return false;
   }

   switch (sensor->type) {
   case SDL_SENSOR_ACCEL:
#ifdef DEBUG_SENSORS
       SDL_sensors[device_index].handlerId = sfwsensor_add_valid_changed_handler(
               sfwsensor,
               &validChangedCB,
               (void*) sensor);
       SDL_sensors[device_index].handlerId = sfwsensor_add_active_changed_handler(
               sfwsensor,
               &activeChangedCB,
               (void*) sensor);
#endif
       SDL_sensors[device_index].handlerId = sfwsensor_add_reading_changed_handler(
               sfwsensor,
               &accelUpdateCB,
               (void*) sensor);
       break;
   case SDL_SENSOR_GYRO:
#ifdef DEBUG_SENSORS
       SDL_sensors[device_index].handlerId = sfwsensor_add_valid_changed_handler(
               sfwsensor,
               &validChangedCB,
               (void*) sensor);
       SDL_sensors[device_index].handlerId = sfwsensor_add_active_changed_handler(
               sfwsensor,
               &activeChangedCB,
               (void*) sensor);
#endif
       SDL_sensors[device_index].handlerId = sfwsensor_add_reading_changed_handler(
               sfwsensor,
               &gyroUpdateCB,
               (void*) sensor);
       break;
   default:
       break;
   }
   sfwsensor_set_datarate(sfwsensor, datarate_hz);
   sfwsensor_start(sfwsensor);
   //sfwsensor_plugin    (SDL_sensors[device_index].sensor);
   //sfwsensor_service   (SDL_sensors[device_index].sensor);
   hwdata->name =      sfwsensor_name(SDL_sensors[device_index].sensor);
   hwdata->object =    sfwsensor_object(SDL_sensors[device_index].sensor);
   hwdata->interface = sfwsensor_interface(SDL_sensors[device_index].sensor);

   hwdata->valid = sfwsensor_is_valid(SDL_sensors[device_index].sensor);
   hwdata->active = sfwsensor_is_active(SDL_sensors[device_index].sensor);

   if (!hwdata->valid) {
       SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM,"Sensor %d (%s) was reported as not valid.", device_index,  hwdata->name);
       //SDL_SetError("Sensor %d (%s) is not valid.", device_index,  hwdata->name);
       //return false;
   }

   SDL_LogVerbose(SDL_LOG_CATEGORY_SYSTEM, "Opened %s, plugin: %s, valid: %d, active: %d",
                  hwdata->name,
                  sfwplugin_name(sfwsensor_plugin(SDL_sensors[device_index].sensor)),
                  hwdata->valid,
                  hwdata->active
                  );
   sensor->hwdata = hwdata;
   return true;
}

static void SDL_SENSORFW_SensorUpdate(SDL_Sensor *sensor)
{
   SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Update");
   g_main_context_iteration (maincontext, false);

   switch (sensor->type) {
   case SDL_SENSOR_ACCEL:
       SensorFW_UpdateAccel(sensor);
       break;
   case SDL_SENSOR_GYRO:
       SensorFW_UpdateGyro(sensor);
       break;
   default:
       break;
   }
}

static void SDL_SENSORFW_SensorClose(SDL_Sensor *sensor)
{
}

static void SDL_SENSORFW_SensorQuit(void)
{
    for (int id = 0; id<SDL_sensors_count; ++id) {
        sfwsensor_remove_handler(SDL_sensors[id].sensor, SDL_sensors[id].handlerId);
        //sfwsensor_stop(SDL_sensors[id].sensor);
        sfwsensor_unref(SDL_sensors[id].sensor);
    }
    g_main_context_unref(maincontext);
}

SDL_SensorDriver SDL_SENSORFW_SensorDriver = {
    SDL_SENSORFW_SensorInit,
    SDL_SENSORFW_SensorGetCount,
    SDL_SENSORFW_SensorDetect,
    SDL_SENSORFW_SensorGetDeviceName,
    SDL_SENSORFW_SensorGetDeviceType,
    SDL_SENSORFW_SensorGetDeviceNonPortableType,
    SDL_SENSORFW_SensorGetDeviceInstanceID,
    SDL_SENSORFW_SensorOpen,
    SDL_SENSORFW_SensorUpdate,
    SDL_SENSORFW_SensorClose,
    SDL_SENSORFW_SensorQuit,
};

static void SensorFW_UpdateAccel(SDL_Sensor *sensor)
{
    SfwSensor *ss = SDL_sensors[0].sensor; // 0 is Accel

    static SfwSampleXyz previous_state = { 0, 0, 0, 0 };
    float data[3];
    Uint64 timestamp = SDL_GetTicksNS();

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
}
static void SensorFW_UpdateGyro(SDL_Sensor *sensor)
{
    SfwSensor *ss = SDL_sensors[1].sensor; // 1 is Gyro

    static SfwSampleXyz previous_state = { 0, 0, 0, 0 };
    float data[3];
    Uint64 timestamp = SDL_GetTicksNS();

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
}

static void accelUpdateCB(SfwSensor *sfwsensor, void* data)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Accel callback!");
    SensorFW_UpdateAccel((SDL_Sensor*) data);
}
static void gyroUpdateCB(SfwSensor *sfwsensor, void* data)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Gyro callback!");
    SensorFW_UpdateGyro((SDL_Sensor*) data);
}
#ifdef DEBUG_SENSORS
static void activeChangedCB(SfwSensor *sfwsensor, void* aptr)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Active changed callback!");
}
static void validChangedCB(SfwSensor *sfwsensor, void* aptr)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Valid changed callback!");
}
#endif

#endif // SDL_SENSOR_SENSORFW
