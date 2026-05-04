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

#include <sensors-glib/sfwreporting.h>
#include <sensors-glib/sfwsensor.h>
#include <sensors-glib/sfwplugin.h>

typedef struct
{
     SDL_SensorType type;
     SfwSensorId ptype;
     SDL_SensorID instance_id;
     SfwSensor *sensor;
} SDL_SensorFWSensor;

struct sensor_hwdata
{
    Uint32 counter;
    unsigned int last_tick;
    Uint64 sensor_timestamp;
};
static SDL_SensorFWSensor *SDL_sensors;
static int SDL_sensors_count;

static void SensorFW_UpdateAccel(SDL_Sensor *sensor);
static void SensorFW_UpdateGyro(SDL_Sensor *sensor);

static bool SDL_SENSORFW_SensorInit(void)
{

    SDL_sensors_count = 14;

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
    return sfwsensor_name(SDL_sensors[device_index].sensor);
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
    SfwReading *hwdata;

    hwdata = (struct SfwReading *)SDL_calloc(1, sizeof(*hwdata));
    if (!hwdata) {
        return false;
    }
    sensor->hwdata = hwdata;

    return true;
}

static void SDL_SENSORFW_SensorUpdate(SDL_Sensor *sensor)
{
}

static void SDL_SENSORFW_SensorClose(SDL_Sensor *sensor)
{
}

static void SDL_SENSORFW_SensorQuit(void)
{
    for (int i = 0; i<SDL_sensors_count; i++) {
        sfwsensor_stop(SDL_sensors[i].sensor);
        sfwsensor_unref(SDL_sensors[i].sensor);
    }
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
     Uint64 timestamp = SDL_GetTicksNS();
     SfwReading* current_state;
     current_state = sfwsensor_reading((SfwSensor*) sensor);
//     if (SDL_memcmp(&previous_state, &current_state, sizeof(SfwSampleAccelerometer)) != 0) {
//         SDL_memcpy(&previous_state, &current_state, sizeof(SfwSampleAccelerometer));
//         data[0] = (float)current_state.x * SDL_STANDARD_GRAVITY;
//         data[1] = (float)current_state.y * SDL_STANDARD_GRAVITY;
//         data[2] = (float)current_state.z * SDL_STANDARD_GRAVITY;
//         SDL_SendSensorUpdate(timestamp, sensor, timestamp, data, sizeof(data));
//     }
}
static void SensorFW_UpdateGyro(SDL_Sensor *sensor)
{
}

#endif // SDL_SENSOR_SENSORFW
