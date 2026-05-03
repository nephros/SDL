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

#ifdef SDL_CAMERA_DRIVER_DROIDMEDIA

#include <stdio.h>
#include <limits.h>
#include "../SDL_syscamera.h"
//#include "SDL_camera_droidmedia.h"

#include "droidmedia/droidmediacamera.h"
#include "droidmedia/droidmediaconstants.h"

#define LOCAL_UNUSED(x) (void)(x)

#if defined(NDEBUG)
#ifdef DEBUG_CAMERA
#undef DEBUG_CAMERA
#endif
#define DEBUG_CAMERA 1
#endif

#define KEY_PARAM_PREVIEW_FMT        "preview-format"
//#define KEY_PARAM_PREVIEW_SIZE       "preferred-preview-size-for-video"
#define KEY_PARAM_PREVIEW_SIZE       "preview-size"
#define KEY_PARAM_PREVIEW_FRAMERATE  "preview-frame-rate"
#define KEY_PARAM_PREVIEW_RATES_LIST "preview-frame-rate-values"
#define KEY_PARAM_VIDEO_SIZES_LIST   "preview-size-values"
#define KEY_PARAM_VIDEO_SIZE         "video-size"
#define KEY_PARAM_FRAME_FORMAT       "video-frame-format"

#ifndef SDL_HAVE_YUV
#pragma message ( "SDL_HAVE_YUV is not defined. This would be useful to have!" )
#endif

// from video/SDL_yuv_c.h
extern bool SDL_CalculateYUVSize(SDL_PixelFormat format, int w, int h, size_t *size, size_t *pitch);
//extern bool SDL_ConvertPixels_YUV_to_RGB(int width, int height, SDL_PixelFormat src_format, SDL_Colorspace src_colorspace, SDL_PropertiesID src_properties, const void *src, int src_pitch, SDL_PixelFormat dst_format, SDL_Colorspace dst_colorspace, SDL_PropertiesID dst_properties, void *dst, int dst_pitch);

// helpers
static CameraFormatAddData DroidCam_camParametersToSDLCaminfo(DroidMediaCamera *camera);

static bool initDroid();
static void DroidCam_setupCallbacks(SDL_Camera* device);
static void DroidCam_setPreviewCallbacksEnabled(SDL_Camera* device, bool);


static void DroidCam_fillCamParameters(SDL_Camera* device);
static char* DroidCam_getCamParameter(DroidMediaCamera* camera, const char* key);
static bool DroidCam_setCamParameter(SDL_Camera* device, const char* key, const char* value);

static SDL_CameraPosition DroidCam_camPositionToSDLPosition(int facing);
static void DroidCam_camFormatToSDLFormats(int fmt, SDL_PixelFormat *format, SDL_Colorspace *colorspace);

// callbacks for droidmedia
static void DroidCam_handleShutter(void* data);
static void DroidCam_handleFocus(void* data, int);
static void DroidCam_handleFocusMove(void* data, int);
static void DroidCam_handleError(void* data, int);
static void DroidCam_handleZoom(void* data, int, int);

static void DroidCam_handleCompressedImage(void *data, DroidMediaData *mem);
static void DroidCam_handlePreviewFrame(void *data, DroidMediaData *mem);
static void DroidCam_handlePostviewFrame(void *data, DroidMediaData *mem);
static void DroidCam_handleVideoFrame(void *data, DroidMediaCameraRecordingData *mem);
static void DroidCam_handleRawImage(void *data, DroidMediaData *mem);
static void DroidCam_handlePreviewMeta(void *data, const DroidMediaCameraFace *faces, size_t num_faces);
static void DroidCam_handleRawImageNotify(void* data);

static bool DroidCam_handleBufferCreated(void *data, DroidMediaBuffer *buf);
static bool DroidCam_handleBufferFrame(void *data, DroidMediaBuffer *buf);
static void DroidCam_handleBuffersReleased(void *data);

DroidMediaPixelFormatConstants  pixelFormats;
DroidMediaColourFormatConstants colorFormats;
DroidMediaCameraConstants       cameraConstants;

typedef struct DroidFrame {
    DroidMediaBufferInfo* info;
    unsigned char*        rawdata;
    size_t                rawsize;
    SDL_PixelFormat       format;

} DroidFrame;

struct SDL_PrivateCameraData
{
  DroidMediaCamera*      droidcam;
  SDL_PropertiesID       parameters;

  DroidFrame*            frame;
  bool                   frameReady;
};

static bool DROIDCAMERA_OpenDevice(SDL_Camera *device, const SDL_CameraSpec *spec)
{
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: OpenDevice wants: %s, device %d: %dx%d, %s, %.2ffps, %s",
                            device->name,
                            device->instance_id,
                            spec->width, spec->height,
                            SDL_GetPixelFormatName(spec->format),
                            (float) ( (float)spec->framerate_numerator / spec->framerate_denominator),
                            device->name);
#endif
    int cameraId;
    SDL_sscanf(device->name, "Droidmedia Camera %d", &cameraId);
    if (!(cameraId >= 0)) {
        return SDL_SetError("Could not determine Camera ID");
    }

    DroidMediaCamera* cam = droid_media_camera_connect(cameraId);
    if (cam == NULL) {
        return SDL_SetError("Could not connect to camera number %d (%s)", cameraId, device->name);
    }

    device->hidden = (struct SDL_PrivateCameraData *) SDL_calloc(1, sizeof (struct SDL_PrivateCameraData));
    device->hidden->frame = (struct DroidFrame *) SDL_calloc(1, sizeof (struct DroidFrame));
    device->hidden->droidcam = cam;
    device->hidden->frameReady = false;

    if (!droid_media_camera_lock (cam)) {
        droid_media_camera_disconnect (cam);
        device->hidden->droidcam = NULL;
        return SDL_SetError("Could not lock camera, disconnected!");
    }

    droid_media_camera_enable_face_detection(cam, DROID_MEDIA_CAMERA_FACE_DETECTION_HW, false);
    droid_media_camera_enable_face_detection(cam, DROID_MEDIA_CAMERA_FACE_DETECTION_SW, false);
    // This switches between "YUV callback mode", and "buffer queue mode"
    //droid_media_camera_store_meta_data_in_buffers(cam, true);


    const float rate = (float)spec->framerate_numerator / spec->framerate_denominator;
    char rate_s[6]; SDL_snprintf(rate_s, sizeof(rate_s), "%d", (const int)rate);
    char res[14]; SDL_snprintf(res, sizeof(res), "%dx%d", spec->width, spec->height);

    DroidCam_fillCamParameters(device);

    SDL_LockProperties(device->hidden->parameters);
//    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_FMT, "yuv421p");
    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_FRAMERATE, rate_s);
    DroidCam_setCamParameter(device, KEY_PARAM_VIDEO_SIZE, res);
    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_SIZE, res);
    DroidCam_setCamParameter(device, "focus-mode", "auto");

    char* framerate     = DroidCam_getCamParameter(cam, KEY_PARAM_PREVIEW_FRAMERATE);
    char* preview_size  = DroidCam_getCamParameter(cam, KEY_PARAM_PREVIEW_SIZE);
    //const char* video_size  = DroidCam_getCamParameter(cam, KEY_PARAM_VIDEO_SIZE);
    char* frame_fmt  = DroidCam_getCamParameter(cam, KEY_PARAM_FRAME_FORMAT);
    if(SDL_strcmp(frame_fmt, "android-opaque")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Camera uses 'Android Opaque' format. Use SDL_HINT_DROIDCAMERA_PIXELFORMAT to specify a pixel format");
    }

    SDL_UnlockProperties(device->hidden->parameters);

    // we will determine the format when we get the first frame.
    // this is allowed here, see SDL_camera.c
    device->actual_spec.format = SDL_PIXELFORMAT_UNKNOWN;

    SDL_sscanf(preview_size, "%ux%u",
        &device->actual_spec.width,
        &device->actual_spec.height);
    SDL_CalculateFraction(SDL_atoi(framerate),
        &device->actual_spec.framerate_numerator,
        &device->actual_spec.framerate_denominator);


    if(!droid_media_camera_start_preview(cam)) {
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Starting camera failed. Are the parameters valid?");
//        return SDL_SetError("Could not start camera preview mode");
    }

    DroidCam_setupCallbacks(device);

    // Currently there is no user permission prompt for camera access, but maybe there will be a D-Bus portal interface at some point.
    SDL_CameraPermissionOutcome(device, true);

    return true;
}

static void DROIDCAMERA_CloseDevice(SDL_Camera *device)
{
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: CloseDevice");
#endif
    if(!device) return;

    if(device->hidden) {
        DroidCam_setPreviewCallbacksEnabled(device, false);
        droid_media_camera_disconnect((DroidMediaCamera*)device->hidden->droidcam);
        device->hidden->droidcam = NULL;

        SDL_DestroyProperties(device->hidden->parameters);

        if (device->hidden->frame->rawdata != NULL) {
            SDL_free(device->hidden->frame->rawdata);
            device->hidden->frame->rawdata = NULL;
        }

        if (device->hidden->frame->info != NULL) {
            SDL_free(device->hidden->frame->info);
            device->hidden->frame->info = NULL;
        }
        if (device->hidden->frame != NULL) {
            SDL_free(device->hidden->frame);
            device->hidden->frame = NULL;
        }
        SDL_free(device->hidden);
        device->hidden = NULL;
    }
    SDL_zero(device->actual_spec);
}

static bool DROIDCAMERA_WaitDevice(SDL_Camera *device)
{
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: WaitDevice");
#endif
    const double duration = ((double) device->actual_spec.framerate_denominator / ((double) device->actual_spec.framerate_numerator));
    while (!SDL_GetAtomicInt(&device->shutdown)) {
        SDL_Delay((Uint32) (duration * 1000.0));
        if(device->hidden->frameReady) {
            break;
        }
    }
    return true;
}

static SDL_CameraFrameResult DROIDCAMERA_AcquireFrame(SDL_Camera *device,
                                                      SDL_Surface *frame,
                                                      Uint64 *timestampNS,
                                                      float *rotation)
{
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: AcquireFrame");
#endif

    DroidMediaBufferInfo* info = device->hidden->frame->info;
    *timestampNS = info->timestamp;
    *rotation = 90;

    frame->pixels = SDL_aligned_alloc(SDL_GetSIMDAlignment(), device->hidden->frame->rawsize);
    if (frame->pixels) {
        SDL_memcpy(frame->pixels, device->hidden->frame->rawdata, device->hidden->frame->rawsize);
        SDL_free(device->hidden->frame->rawdata);
        frame->pitch = info->stride;
        frame->w = info->width;
        frame->h = info->height;
        device->hidden->frame->rawdata = NULL;
        device->hidden->frame->rawsize = 0;
        device->hidden->frameReady = false;
        DroidCam_setPreviewCallbacksEnabled(device, true);

#ifdef DEBUG_CAMERA
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: Frame copied %dx%d, fmt %s",
                                                frame->w, frame->h,
                                              SDL_GetPixelFormatName(frame->format));
#endif
        return SDL_CAMERA_FRAME_READY;
    }

    return SDL_CAMERA_FRAME_ERROR;
}

static void DROIDCAMERA_ReleaseFrame(SDL_Camera *device, SDL_Surface *frame)
{
LOCAL_UNUSED(device);
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: ReleaseFrame");
#endif
    SDL_aligned_free(frame->pixels);
}

static void DROIDCAMERA_DetectDevices(void)
{
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: DetectDevices");
#endif

    int num_cameras = droid_media_camera_get_number_of_cameras();
    if (num_cameras == 0) {
        SDL_SetError("No Cameras found!");
        return;
    }

    for (int num = 0; num < num_cameras; num++) {
        DroidMediaCamera *cam = droid_media_camera_connect(num);
        if (cam == NULL) {
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Could not connect to camera %d!", num);
            continue;
        }

        DroidMediaCameraInfo info;
        if (!droid_media_camera_get_info(&info, num)) {
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Could not get info for camera %d:", num);
            continue;
        }

        SDL_CameraPosition position = DroidCam_camPositionToSDLPosition(info.facing);

        char fullname[24];
        SDL_snprintf(fullname, sizeof(fullname),"Droidmedia Camera %d", num);

        CameraFormatAddData add_data = DroidCam_camParametersToSDLCaminfo(cam);

        if (add_data.num_specs > 0) {
            SDL_Camera *device = SDL_AddCamera((const char*)fullname,
                                                position,
                                                add_data.num_specs, add_data.specs,
                                                &num
            );
#ifdef DEBUG_CAMERA
            if (!device) {
                SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Adding Camera %d failed.", num);
            } else {
                SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Added Camera %d as %s (%d modes).", num, fullname, add_data.num_specs);
            }
        } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Camera %d had no specs to add.", num);
#endif
        }

        droid_media_camera_disconnect(cam);
        SDL_free(add_data.specs);
    }
}

static void DROIDCAMERA_FreeDeviceHandle(SDL_Camera *device)
{
LOCAL_UNUSED(device);
    /* NOP */
}

static void DROIDCAMERA_Deinitialize(void)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Deinitialize");
    droid_media_deinit();
}

static bool DROIDCAMERA_Init(SDL_CameraDriverImpl *impl)
{
    impl->DetectDevices    = DROIDCAMERA_DetectDevices;
    impl->OpenDevice       = DROIDCAMERA_OpenDevice;
    impl->CloseDevice      = DROIDCAMERA_CloseDevice;
    impl->WaitDevice       = DROIDCAMERA_WaitDevice;
    impl->AcquireFrame     = DROIDCAMERA_AcquireFrame;
    impl->ReleaseFrame     = DROIDCAMERA_ReleaseFrame;
    impl->FreeDeviceHandle = DROIDCAMERA_FreeDeviceHandle;
    impl->Deinitialize     = DROIDCAMERA_Deinitialize;
    //impl->ProvidesOwnCallbackThread = true;

    return initDroid();
}

CameraBootStrap DROIDCAMERA_bootstrap = {
    // TODO: the last paramter is demand_only; // if true: request explicitly, or it won't be available.
    "droidcamera", "SDL droidmedia camera driver", DROIDCAMERA_Init, true
};

static bool DroidCam_isReadonlyCamProperty(const char* key)
{
    return (SDL_endswith(key, "-values"))
        || (SDL_endswith(key, "-supported"))
        || (SDL_endswith(key, "-range"))
        || (SDL_startswith(key, "max-"))
        || (SDL_startswith(key, "min-"))
        || (SDL_strcmp(key, "focus-distances") == 0)
        || (SDL_strcmp(key, "focus-areas") == 0)
        || (SDL_strcmp(key, "metering-areas") == 0)
//        || (SDL_strcmp(key, "video-stabilization") == 0)
        || (SDL_strcmp(key, "video-frame-format") == 0)
        || (SDL_strcmp(key, "preferred-preview-size-for-video") == 0)
        || (SDL_strcmp(key, "zoom-ratios") == 0);
 }

static bool DroidCam_setCamParameter(SDL_Camera* device, const char* key, const char* value)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Setting camera parameter: %s=%s", key, value);
    SDL_PropertiesID props = device->hidden->parameters;
    if(DroidCam_isReadonlyCamProperty(key)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Read-only Parameter: %s, ignored!", key);
        return false;
    }
    if (!SDL_HasProperty(props, key)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Unknown Parameter: %s, ignored!", key);
        return false;
    }
    SDL_SetStringProperty(props, key, value);
    device->hidden->parameters = props;

    char parm[SDL_strlen(key) + SDL_strlen(value) +1];
    SDL_snprintf(parm, SDL_strlen(key) + SDL_strlen(value) +2, "%s=%s", key, value);
    return droid_media_camera_set_parameters(device->hidden->droidcam, parm);
}

static void DroidCam_fillCamParameters(SDL_Camera* device)
{
    char *parms = droid_media_camera_get_parameters(device->hidden->droidcam);
    if (parms == NULL) {
        SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Could not get parameters from camera!");
        return;
    }

    SDL_PropertiesID props = SDL_CreateProperties();

    char *pair = NULL;
    uint32_t numparm = 0;
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Current camera parameters:");
#endif
    while ((pair = SDL_strtok_r(parms, ";", &parms))) {
        const char* equal_sign = strchr(pair, '=');
        const char* value_start = equal_sign + 1;

        size_t key_len = equal_sign - pair;
        size_t value_len = strlen(value_start);
        char key[key_len+1];
        SDL_strlcpy(key, pair, key_len+1);
        char value[value_len+1];
        SDL_strlcpy(value, value_start, value_len+1);

        if(SDL_SetStringProperty(props, key, value)) {
#if DEBUG_CAMERA
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "\t%s=%s", key, value); 
#endif
            numparm++;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Could not set parameter: %s=%s", key, value); 
        }
    }
    device->hidden->parameters = props;
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Camera reported %u parameters.", numparm);
}

static char* DroidCam_getCamParameter(DroidMediaCamera* camera, const char* key)
{

    const char *parms = droid_media_camera_get_parameters(camera);
    char search_pattern[SDL_strlen(key)+2];
    SDL_snprintf(search_pattern, sizeof(search_pattern), "%s=", key);

    char* candidate = strstr(parms, search_pattern);
    if (!candidate) { return NULL; }
    size_t offset = strlen(search_pattern); // Move past the key and equals sign
    char* value_start = candidate + offset;

    char* end = strchr(value_start, ';'); // Find the end delimiter (semicolon or end of string)
    if (end) { // Value ends at semicolon
        size_t value_length = end - value_start;
        static char value[256];
        SDL_strlcpy(value, value_start, value_length+1);
        return SDL_strdup(value);
    } else { // Value goes until end of string
        return SDL_strdup(value_start);
    }
}

static CameraFormatAddData DroidCam_camParametersToSDLCaminfo(DroidMediaCamera *camera)
{
    CameraFormatAddData data;
    SDL_zero(data);

    const int32_t fmt = droid_media_camera_get_video_color_format(camera);

    const char* video_sizes    = DroidCam_getCamParameter(camera, KEY_PARAM_VIDEO_SIZES_LIST);
    const char* rates          = DroidCam_getCamParameter(camera, KEY_PARAM_PREVIEW_RATES_LIST);
//    const char* preview_format = DroidCam_getCamParameter(camera, KEY_PARAM_PREVIEW_FMT);

    SDL_PixelFormat pixelformat = SDL_PIXELFORMAT_UNKNOWN;
    SDL_Colorspace colorspace = SDL_COLORSPACE_UNKNOWN;
    DroidCam_camFormatToSDLFormats(fmt, &pixelformat, &colorspace);

    int framerate_n, framerate_d;
    Uint32 w, h;

    char* video_size = NULL;
    char* rest = SDL_strdup(video_sizes);
    while ((video_size = SDL_strtok_r(rest, ",", &rest))) {
        SDL_sscanf(video_size, "%ux%u", &w, &h);
        char* rt = NULL;
        char* rrest = SDL_strdup(rates);
        while ((rt = SDL_strtok_r(rrest, ",", &rrest))) {
            SDL_CalculateFraction(SDL_atoi(rt), &framerate_n, &framerate_d);
            SDL_AddCameraFormat(&data, pixelformat, colorspace, (int) w, (int) h, (int)framerate_n, (int)framerate_d);
        }
    }
    return data;
}

static void DroidCam_camFormatToSDLFormats(int fmt, SDL_PixelFormat *format, SDL_Colorspace *colorspace)
{
    SDL_PixelFormat pxf = SDL_PIXELFORMAT_YV12;
    SDL_Colorspace csp  = SDL_COLORSPACE_BT709_LIMITED;
    if       (fmt == pixelFormats.HAL_PIXEL_FORMAT_YV12)        { pxf = SDL_PIXELFORMAT_YV12;     csp = SDL_COLORSPACE_BT709_LIMITED;
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_RGB_565)     { pxf = SDL_PIXELFORMAT_RGB565;   csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_RGB_888)     { pxf = SDL_PIXELFORMAT_XRGB32;   csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_RGBA_8888)   { pxf = SDL_PIXELFORMAT_RGBA32;   csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_RGBX_8888)   { pxf = SDL_PIXELFORMAT_RGBX32;   csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_BGRA_8888)   { pxf = SDL_PIXELFORMAT_BGRA32;   csp = SDL_COLORSPACE_SRGB;
    // FIXME: are these correct?
    } else if(fmt == pixelFormats.HAL_PIXEL_FORMAT_YCrCb_420_SP)  { pxf = SDL_PIXELFORMAT_NV12; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if (fmt == pixelFormats.HAL_PIXEL_FORMAT_YCbCr_422_SP) { pxf = SDL_PIXELFORMAT_YV12; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if (fmt == pixelFormats.HAL_PIXEL_FORMAT_YCbCr_422_I)  { pxf = SDL_PIXELFORMAT_UYVY; csp = SDL_COLORSPACE_YUV_DEFAULT;
    // https://developer.android.com/reference/android/graphics/ImageFormat#RAW_SENSOR
    // a single-channel Bayer-mosaic image. Each pixel color sample is stored with 16 bits of precision.
    // } else if (fmt == pixelFormats.HAL_PIXEL_FORMAT_RAW_SENSOR
    // FIXME: what to use here?
    } else if (fmt == pixelFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar32m)            { pxf = SDL_PIXELFORMAT_EXTERNAL_OES; csp = SDL_COLORSPACE_BT601_LIMITED;
    } else if (fmt == pixelFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka) { pxf = SDL_PIXELFORMAT_EXTERNAL_OES; csp = SDL_COLORSPACE_BT601_LIMITED;
    // from DroidMediaColourFormatConstants
    } else if(fmt == colorFormats.OMX_COLOR_Format32bitARGB8888)    { pxf = SDL_PIXELFORMAT_ARGB32; csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == colorFormats.OMX_COLOR_Format32bitBGRA8888)    { pxf = SDL_PIXELFORMAT_BGRA32; csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == colorFormats.OMX_COLOR_Format16bitRGB565)      { pxf = SDL_PIXELFORMAT_RGB565; csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == colorFormats.OMX_COLOR_Format16bitBGR565)      { pxf = SDL_PIXELFORMAT_BGR565; csp = SDL_COLORSPACE_SRGB;
    } else if(fmt == colorFormats.OMX_COLOR_FormatYUV420Planar)     { pxf = SDL_PIXELFORMAT_YV12; csp = SDL_COLORSPACE_YUV_DEFAULT;
//    } else if(fmt == colorFormats.OMX_COLOR_FormatYUV420Flexible)   { pxf = SDL_PIXELFORMAT_YV12; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if(fmt == colorFormats.OMX_COLOR_FormatYCbYCr)           { pxf = SDL_PIXELFORMAT_YV12; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if(fmt == colorFormats.OMX_COLOR_FormatYUV420SemiPlanar) { pxf = SDL_PIXELFORMAT_NV21; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if(fmt == colorFormats.OMX_COLOR_FormatYUV422SemiPlanar) { pxf = SDL_PIXELFORMAT_YUY2; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else if(fmt == 0x7F000789) {
        SDL_PixelFormat *hintformat = (SDL_PixelFormat*) SDL_GetHint(SDL_HINT_DROIDCAMERA_PIXELFORMAT);
        if (hintformat == NULL) {
            pxf = SDL_PIXELFORMAT_NV21; csp = SDL_COLORSPACE_BT709_LIMITED;
        } else {
            pxf = *hintformat;
            if(SDL_ISPIXELFORMAT_FOURCC(*hintformat)) {
              csp = SDL_COLORSPACE_BT709_LIMITED;
            } else {
              csp = SDL_COLORSPACE_RGB_DEFAULT;
            }
        }
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Did not find format, returning default.");
    }
    *format = pxf;
    *colorspace = csp;
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Looked up format: %u (0x%x), returning %s", fmt, fmt, SDL_GetPixelFormatName(pxf));
#endif
 }

static SDL_CameraPosition DroidCam_camPositionToSDLPosition(int facing)
{
    if (facing == DROID_MEDIA_CAMERA_FACING_FRONT) { return SDL_CAMERA_POSITION_FRONT_FACING; }
    else if (facing == DROID_MEDIA_CAMERA_FACING_BACK) { return SDL_CAMERA_POSITION_BACK_FACING; }
    return SDL_CAMERA_POSITION_UNKNOWN;
}

static bool initDroid()
{
    if(!droid_media_init()) {
        SDL_SetError("Could not initialize droidmedia!");
        return false;
    }

    droid_media_pixel_format_constants_init(&pixelFormats);
    droid_media_camera_constants_init(&cameraConstants);
    droid_media_colour_format_constants_init(&colorFormats);
#if DEBUG_CAMERA
SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Initialized Pixel Formats:\n\n\
  %d: HAL_PIXEL_FORMAT_RGBA_8888\n\
  %d: HAL_PIXEL_FORMAT_RGBX_8888\n\
  %d: HAL_PIXEL_FORMAT_RGB_888\n\
  %d: HAL_PIXEL_FORMAT_RGB_565\n\
  %d: HAL_PIXEL_FORMAT_BGRA_8888\n\
  %d: HAL_PIXEL_FORMAT_YV12\n\
  %d: HAL_PIXEL_FORMAT_RAW_SENSOR\n\
  %d: HAL_PIXEL_FORMAT_YCrCb_420_SP\n\
  %d: HAL_PIXEL_FORMAT_YCbCr_422_SP\n\
  %d: HAL_PIXEL_FORMAT_YCbCr_422_I\n\
  %d: QOMX_COLOR_FormatYUV420PackedSemiPlanar32m\n\
  %d: QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka\n\n\
",
  pixelFormats.HAL_PIXEL_FORMAT_RGBA_8888,
  pixelFormats.HAL_PIXEL_FORMAT_RGBX_8888,
  pixelFormats.HAL_PIXEL_FORMAT_RGB_888,
  pixelFormats.HAL_PIXEL_FORMAT_RGB_565,
  pixelFormats.HAL_PIXEL_FORMAT_BGRA_8888,
  pixelFormats.HAL_PIXEL_FORMAT_YV12,
  pixelFormats.HAL_PIXEL_FORMAT_RAW_SENSOR,
  pixelFormats.HAL_PIXEL_FORMAT_YCrCb_420_SP,
  pixelFormats.HAL_PIXEL_FORMAT_YCbCr_422_SP,
  pixelFormats.HAL_PIXEL_FORMAT_YCbCr_422_I,
  pixelFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar32m,
  pixelFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka
);

SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Initialized Color Formats:\n\n\
  %d: QOMX_COLOR_FormatYUV420PackedSemiPlanar32m\n\
  %d: QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka\n\
  %d: OMX_COLOR_FormatYUV420Planar\n\
  %d: OMX_COLOR_FormatYUV420PackedPlanar\n\
  %d: OMX_COLOR_FormatYUV420SemiPlanar\n\
  %d: OMX_COLOR_FormatYUV422SemiPlanar\n\
  %d: OMX_COLOR_FormatL8\n\
  %d: OMX_COLOR_FormatYCbYCr\n\
  %d: OMX_COLOR_FormatYCrYCb\n\
  %d: OMX_COLOR_FormatCbYCrY\n\
  %d: OMX_COLOR_Format32bitARGB8888\n\
  %d: OMX_COLOR_Format32bitBGRA8888\n\
  %d: OMX_COLOR_Format16bitRGB565\n\
  %d: OMX_COLOR_Format16bitBGR565\n\
",
  colorFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar32m,
  colorFormats.QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka,
  colorFormats.OMX_COLOR_FormatYUV420Planar,
  colorFormats.OMX_COLOR_FormatYUV420PackedPlanar,
  colorFormats.OMX_COLOR_FormatYUV420SemiPlanar,
  colorFormats.OMX_COLOR_FormatYUV422SemiPlanar,
  colorFormats.OMX_COLOR_FormatL8,
  colorFormats.OMX_COLOR_FormatYCbYCr,
  colorFormats.OMX_COLOR_FormatYCrYCb,
  colorFormats.OMX_COLOR_FormatCbYCrY,
  colorFormats.OMX_COLOR_Format32bitARGB8888,
  colorFormats.OMX_COLOR_Format32bitBGRA8888,
  colorFormats.OMX_COLOR_Format16bitRGB565,
  colorFormats.OMX_COLOR_Format16bitBGR565
);
#endif
    return true;
}

static void DroidCam_setupCallbacks(SDL_Camera* device)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Setting up callbacks");
    {
        DroidMediaCameraCallbacks cb;
        cb.shutter_cb = DroidCam_handleShutter;
        cb.focus_cb = DroidCam_handleFocus;
        cb.focus_move_cb = DroidCam_handleFocusMove;
        cb.error_cb = DroidCam_handleError;
        cb.zoom_cb =   DroidCam_handleZoom;

        cb.raw_image_cb = DroidCam_handleRawImage;
        cb.compressed_image_cb =  DroidCam_handleCompressedImage;
        cb.postview_frame_cb = DroidCam_handlePostviewFrame;
        cb.raw_image_notify_cb = DroidCam_handleRawImageNotify;
        cb.preview_frame_cb = DroidCam_handlePreviewFrame;

        cb.preview_metadata_cb = DroidCam_handlePreviewMeta;
        cb.video_frame_cb = DroidCam_handleVideoFrame;

        droid_media_camera_set_callbacks(device->hidden->droidcam, &cb, device );

        DroidCam_setPreviewCallbacksEnabled(device, true);
    }
#if 0
    {
        DroidMediaBufferQueueCallbacks cb;
        DroidMediaBufferQueue* queue = droid_media_camera_get_buffer_queue (device->hidden->droidcam);

        cb.buffers_released = DroidCam_handleBuffersReleased;
        cb.frame_available = DroidCam_handleBufferFrame;
        cb.buffer_created = DroidCam_handleBufferCreated;

        droid_media_buffer_queue_set_callbacks (queue, &cb, device);
    }
#endif
}

static void DroidCam_handleError(void* data, int error) {
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleError: %d", error);
#endif
    SDL_Camera* dev = (SDL_Camera*)data;
    char errorName[25];
    if (error == cameraConstants.CAMERA_ERROR_UNKNOWN) {
        SDL_snprintf(errorName, sizeof(errorName), "Unknown Error");
    } else if (cameraConstants.CAMERA_ERROR_RELEASED) {
        SDL_snprintf(errorName, sizeof(errorName), "Camera released.");
    } else if (cameraConstants.CAMERA_ERROR_SERVER_DIED) {
        SDL_snprintf(errorName, sizeof(errorName), "Camera backend crashed.");
    } else {
        SDL_snprintf(errorName, sizeof(errorName), "Unknown error code 0x%x", error);
    }
    SDL_LogError(SDL_LOG_CATEGORY_ERROR, "'%s': %s", dev->name, errorName);
}

static void DroidCam_handlePreviewMeta(void *data, const DroidMediaCameraFace *faces, size_t num_faces)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(faces);
LOCAL_UNUSED(num_faces);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handlePreviewMeta not implemented");
#endif
}

static void DroidCam_handleVideoFrame(void *data, DroidMediaCameraRecordingData *mem)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleVideoFrame not implemented");
#endif
}

static void DroidCam_handlePostviewFrame(void *data, DroidMediaData *mem)
{
    DroidCam_handlePreviewFrame(data, mem);
}

static void DroidCam_handlePreviewFrame(void *data, DroidMediaData *mem)
{
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handlePreviewFrame");
#endif

    SDL_Camera* device = (SDL_Camera*)data;
    // DroidMediaBufferInfo is actually for buffer callbacks.
    // But to be able to use the struct from both, fake it here:
    if (device->hidden->frame->info == NULL) {
        device->hidden->frame->info = (DroidMediaBufferInfo*) SDL_calloc(1, sizeof (DroidMediaBufferInfo));
    }
 
    // first frame, read out format and store it.
    if(device->hidden->frame->info->frame_number == 0) {
      droid_media_camera_start_auto_focus(device->hidden->droidcam);
        const char* preview_format = DroidCam_getCamParameter(device->hidden->droidcam, KEY_PARAM_PREVIEW_FMT);
        const char* preview_size  = DroidCam_getCamParameter(device->hidden->droidcam, KEY_PARAM_PREVIEW_SIZE);
        if (SDL_strcmp(preview_format, "yuv420p") == 0) {
            device->hidden->frame->format = SDL_PIXELFORMAT_YV12;
               //SDL_DEFINE_PIXELFOURCC('Y','V','2','1');
        } else if (SDL_strcmp(preview_format, "yuv420sp") == 0) { // onC2, the selfie cam is this, a.k.a. I420.
            device->hidden->frame->format = SDL_PIXELFORMAT_NV21;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: Unknown preview format: %s", preview_format);
        }
        Uint32 w, h;
        SDL_sscanf(preview_size, "%ux%u", &w, &h);
        device->hidden->frame->info->width = w;
        device->hidden->frame->info->height = h;
#if DEBUG_CAMERA
        const int32_t camformat = droid_media_camera_get_video_color_format (device->hidden->droidcam);
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: Camera reported format: %d (0x%x)", camformat, camformat);
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: Camera previewformat: %s (%s)",
                                              preview_format,
                                              SDL_GetPixelFormatName(device->hidden->frame->format));
#endif
    }

    if(device->hidden->frameReady == true) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: last frame not processed, skipping");
    } else {
        int width = device->hidden->frame->info->width;
        int height = device->hidden->frame->info->height;
        SDL_PixelFormat format = device->hidden->frame->format;
        size_t pitch;
        size_t expected_size;
        SDL_CalculateYUVSize(format, width, height, &expected_size, &pitch);
#if DEBUG_CAMERA
        if (expected_size != mem->size) {
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame buffersize %ld did not match expected %ld", mem->size, expected_size);
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame yuvpitch: %ld", pitch);
            SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame wxh: %dx%d", width, height);
        }
#endif
        device->hidden->frame->rawdata = SDL_malloc(mem->size);
        SDL_memcpy(device->hidden->frame->rawdata, mem->data, mem->size);
        device->hidden->frame->rawsize = mem->size;

        device->hidden->frame->info->width  = width;
        device->hidden->frame->info->height = height;
        device->hidden->frame->info->stride = pitch;
        device->hidden->frame->info->timestamp = SDL_GetTicksNS(); // oh well, close enough.
        device->hidden->frame->info->frame_number += 1;
        device->hidden->frameReady = true;
#if DEBUG_CAMERA
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: prepared frame %ld, p %ld, fmt %s ",
                                              device->hidden->frame->info->frame_number,
                                              pitch,
                                              SDL_GetPixelFormatName(format));
#endif
    }
}

/* a taken picture, jpeg format */
static void DroidCam_handleCompressedImage(void *data, DroidMediaData *mem)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleCompressedImage not implemented");
#endif
}

static void DroidCam_handleRawImage(void *data, DroidMediaData *mem) {
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleRawImage not implemented");
#endif
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
}

static void DroidCam_handleBuffersReleased(void *data)
{
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferReleased");
#endif
LOCAL_UNUSED(data);
}

static bool DroidCam_handleBufferCreated(void *data, DroidMediaBuffer *buf)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(buf);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferCreate");
#endif
    return true;
}

static bool DroidCam_handleBufferFrame(void *data, DroidMediaBuffer *buf)
{
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferFrame");
#endif
    SDL_Camera* device = (SDL_Camera*) data;

    device->hidden->frame->info = (DroidMediaBufferInfo*) SDL_calloc(1, sizeof (DroidMediaBufferInfo));

    droid_media_buffer_lock(buf, DROID_MEDIA_BUFFER_LOCK_READ);
    droid_media_buffer_get_info(buf, device->hidden->frame->info);
    DroidMediaBufferInfo *info = device->hidden->frame->info;

    SDL_PixelFormat pfx; SDL_Colorspace csp;
    DroidCam_camFormatToSDLFormats(info->format, &pfx, &csp);

    /* ... do something here... */
    droid_media_buffer_unlock(buf);
    droid_media_buffer_release(buf, NULL, NULL);
    droid_media_buffer_destroy(buf);
    return true;
}

static void DroidCam_setPreviewCallbacksEnabled(SDL_Camera *device, bool enable) {
#if DEBUG_CAMERA
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> Preview callbacks %s.", enable ? "enabled" : "disabled");
#endif
        int cbflag = enable
                   ? cameraConstants.CAMERA_FRAME_CALLBACK_FLAG_CAMERA
                   //? cameraConstants.CAMERA_FRAME_CALLBACK_FLAG_CAMCORDER
                   : cameraConstants.CAMERA_FRAME_CALLBACK_FLAG_NOOP;
        droid_media_camera_set_preview_callback_flags(device->hidden->droidcam, cbflag);
        droid_media_camera_send_command(device->hidden->droidcam, cameraConstants.CAMERA_CMD_PING, 0, 0);
}

static void DroidCam_handleRawImageNotify(void* data)
{
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleRawImageNotify not implemented");
#endif

LOCAL_UNUSED(data);
}
static void DroidCam_handleShutter(void* data)
{
LOCAL_UNUSED(data);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleShutter not implemented");
#endif
}
static void DroidCam_handleFocus(void* data, int num)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleFocus not implemented");
#endif
}
static void DroidCam_handleFocusMove(void* data, int num)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleFocusMove not implemented");
#endif
}
static void DroidCam_handleZoom(void* data, int num1, int num2)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num1);
LOCAL_UNUSED(num2);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleZoom not implemented");
#endif
}

#endif  // SDL_CAMERA_DRIVER_DROIDMEDIA
