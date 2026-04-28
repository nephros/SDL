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
#include "SDL_camera_droidmedia.h"

#include "droidmediaconvert.h"

#define LOCAL_UNUSED(x) (void)(x)

#if defined(NDEBUG)
#ifdef DEBUG_CAMERA
#undef DEBUG_CAMERA
#endif
#define DEBUG_CAMERA 1
#endif

#define KEY_PARAM_PREVIEW_FMT        "preview-format"
#define KEY_PARAM_PREVIEW_SIZE       "preferred-preview-size-for-video"
#define KEY_PARAM_PREVIEW_FRAMERATE  "preview-frame-rate"
#define KEY_PARAM_PREVIEW_RATES_LIST "preview-frame-rate-values"
#define KEY_PARAM_VIDEO_SIZES_LIST   "preview-size-values"
#define KEY_PARAM_VIDEO_SIZE         "video-size"

#ifndef SDL_HAVE_YUV
#pragma message ( "SDL_HAVE_YUV is not defined. This would be useful to have!" )
#endif

// from video/SDL_yuv_c.h
extern bool SDL_CalculateYUVSize(SDL_PixelFormat format, int w, int h, size_t *size, size_t *pitch);
extern bool SDL_ConvertPixels_YUV_to_RGB(int width, int height, SDL_PixelFormat src_format, SDL_Colorspace src_colorspace, SDL_PropertiesID src_properties, const void *src, int src_pitch, SDL_PixelFormat dst_format, SDL_Colorspace dst_colorspace, SDL_PropertiesID dst_properties, void *dst, int dst_pitch);

DroidMediaPixelFormatConstants  pixelFormats;
DroidMediaColourFormatConstants colorFormats;
DroidMediaCameraConstants       cameraConstants;

typedef struct DroidFrame {
    SDL_Surface*          data;
    DroidMediaBufferInfo* info;

    void*                 rawdata;
    ssize_t               rawsize;

} DroidFrame;

struct SDL_PrivateCameraData
{
  DroidMediaCamera*      droidcam;
  SDL_PropertiesID*      parameters;

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
        SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "Could not lock camera, disconnecting!");
        droid_media_camera_disconnect (cam);
        device->hidden->droidcam = NULL;
        return SDL_SetError("Could not lock camera, disconnected!");
    }

    droid_media_camera_enable_face_detection(cam, DROID_MEDIA_CAMERA_FACE_DETECTION_HW, false);
    droid_media_camera_enable_face_detection(cam, DROID_MEDIA_CAMERA_FACE_DETECTION_SW, false);

    const float rate = (float)spec->framerate_numerator / spec->framerate_denominator;
    char rate_s[6]; SDL_snprintf(rate_s, sizeof(rate_s), "%d", (const int)rate);
    char res[14]; SDL_snprintf(res, sizeof(res), "%dx%d", spec->width, spec->height);

    DroidCam_fillCamParameters(device);

    SDL_LockProperties(*device->hidden->parameters);
    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_FMT, "yuv420p");
    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_FRAMERATE, rate_s);
//    DroidCam_setCamParameter(device, KEY_PARAM_VIDEO_SIZE, res);
    DroidCam_setCamParameter(device, KEY_PARAM_PREVIEW_SIZE, res);

    /* FIXME FIXME: this kills the camera??? */
    //if(!DroidCam_commitCamParameters(device)) {
    //    SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Setting parameters failed!");
    //}

    const char* framerate     = DroidCam_getCamParameter(cam, KEY_PARAM_PREVIEW_FRAMERATE);
    const char* preview_size  = DroidCam_getCamParameter(cam, KEY_PARAM_PREVIEW_SIZE);
    //const char* video_size  = DroidCam_getCamParameter(cam, KEY_PARAM_VIDEO_SIZE);

    // FIXME: we should do that in a custom method probably...
    SDL_SetStringProperty(*device->hidden->parameters, KEY_PARAM_PREVIEW_FRAMERATE, framerate );
    SDL_SetStringProperty(*device->hidden->parameters, "preview-size", preview_size );

    SDL_UnlockProperties(*device->hidden->parameters);

    int32_t camfmt = droid_media_camera_get_video_color_format (cam);
    SDL_PixelFormat pixelformat; //= SDL_PIXELFORMAT_UNKNOWN;
    SDL_Colorspace colorspace; // = SDL_COLORSPACE_UNKNOWN;
    DroidCam_camFormatToSDLFormats(camfmt, &pixelformat, &colorspace);
    Uint32 w, h;
    SDL_sscanf(preview_size, "%ux%u", &w, &h);
    device->actual_spec.format = pixelformat;
    device->actual_spec.width = w;
    device->actual_spec.height = h;
    device->actual_spec.framerate_numerator = SDL_atoi(framerate);
    device->actual_spec.framerate_denominator = 1;


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
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: CloseDevice");
    DroidCam_setPreviewCallbacksEnabled(device, false);
    droid_media_camera_unlock((DroidMediaCamera*)device->hidden->droidcam);
    droid_media_camera_disconnect((DroidMediaCamera*)device->hidden->droidcam);
    device->hidden->droidcam = NULL;

    SDL_DestroyProperties(*device->hidden->parameters);
    device->hidden->parameters = NULL;

    if (device->hidden->frame->data != NULL) {
        SDL_DestroySurface(device->hidden->frame->data);
        device->hidden->frame->data = NULL;
    }
    if (device->hidden->frame->info != NULL) {
        SDL_free(device->hidden->frame->info);
        device->hidden->frame->info = NULL;
    }

    SDL_free(device->hidden);
    device->hidden = NULL;
}

static bool DROIDCAMERA_WaitDevice(SDL_Camera *device)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: WaitDevice");
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
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: AcquireFrame");

    if (!device->hidden->frameReady) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: No Frame available");
        return SDL_CAMERA_FRAME_SKIP;
    }
    DroidMediaBufferInfo* info = device->hidden->frame->info;
    *timestampNS = info->timestamp;
    *rotation = 90;
    static uint64_t frame_handled = 0;
    static SDL_Time stamp_handled = 0;

    if((frame_handled > 0) && (frame_handled >= info->frame_number) ) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: frame %ld already handled.", info->frame_number);
        device->hidden->frameReady = false;
        return SDL_CAMERA_FRAME_SKIP;
    }

    if((stamp_handled > 0) && (stamp_handled >= info->timestamp) ) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: timestamp already handled.");
        device->hidden->frameReady = false;
        return SDL_CAMERA_FRAME_SKIP;
    }

    if (!device->hidden->frameReady) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: No Frame available, skipped");
        return SDL_CAMERA_FRAME_SKIP;
    }

    if(device->hidden->frame->rawsize == 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: size is 0, frame skipped");
        return SDL_CAMERA_FRAME_SKIP;
    }
    if(info->stride == 0) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: stride is 0, frame skipped");
        return SDL_CAMERA_FRAME_SKIP;
    }
#ifdef DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: got frame %ld/%lu, size/stride: %d",
                                           info->frame_number, info->timestamp, info->stride);
#endif
    //frame->pixels = SDL_aligned_alloc(SDL_GetSIMDAlignment(), info->width * info->height);
    frame->pixels = SDL_aligned_alloc(SDL_GetSIMDAlignment(), device->hidden->frame->rawsize);
    if (frame->pixels) {
        SDL_memcpy(frame->pixels, device->hidden->frame->rawdata, device->hidden->frame->rawsize);
        SDL_free(device->hidden->frame->rawdata);
        device->hidden->frame->rawsize = 0;

        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: Acquire: rendered image.");
        device->hidden->frameReady = false;
        DroidCam_setPreviewCallbacksEnabled(device, true);

        frame->pitch = info->width; // pitch == width for YUV
        if (!SDL_ISPIXELFORMAT_FOURCC(info->format)) {
            frame->pitch *= SDL_BYTESPERPIXEL(info->format);
        }

        frame_handled = info->frame_number;
        return SDL_CAMERA_FRAME_READY;
    }

    return SDL_CAMERA_FRAME_ERROR;
}

static void DROIDCAMERA_ReleaseFrame(SDL_Camera *device, SDL_Surface *frame)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: ReleaseFrame");
    SDL_aligned_free(frame->pixels);
}

static void DROIDCAMERA_DetectDevices(void)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: DetectDevices");

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
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: FreeDeviceHandle");
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

static void SDLCALL concatCamProperties(void *userdata, SDL_PropertiesID props, const char *name)
{
      const char* value = SDL_GetStringProperty(props, name, "");
      char* result = (char*) userdata;
      size_t pair_len = SDL_strlen(name) + SDL_strlen(value);
      char pair[pair_len+3];
      SDL_snprintf(pair, pair_len+3, "%s=%s;", name, value);
      SDL_strlcat(result, pair, 4096);
      userdata = SDL_strdup(result);

}

static const char* buildParameterString(const SDL_PropertiesID props)
{
    char finalprops[] = "";
    SDL_EnumerateProperties(props, concatCamProperties, finalprops);
    return SDL_strdup(finalprops);
}

static bool DroidCam_commitCamParameters(SDL_Camera* device)
{
    SDL_PropertiesID props = *device->hidden->parameters;
    const char* finalprops = buildParameterString(props);
#if DEBUG_CAMERA
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Final camera parameters: %s", finalprops);
#endif
    return droid_media_camera_set_parameters(device->hidden->droidcam, finalprops);
}

static bool DroidCam_setCamParameter(SDL_Camera* device, const char* key, const char* value)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Setting camera parameter: %s=%s", key, value);
    SDL_PropertiesID props = *device->hidden->parameters;
    if (!SDL_HasProperty(props, key)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_SYSTEM, "Unknown Parameter: %s, ignored!", key);
        return false;
    }
    SDL_SetStringProperty(props, key, value);
    device->hidden->parameters = &props;
    return true;
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
            SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "\t%s=%s", key, value); 
            numparm++;
        }
    }
    device->hidden->parameters = &props;
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

    const char* preview_size = DroidCam_getCamParameter(camera, KEY_PARAM_PREVIEW_SIZE);
    const char* framerate    = DroidCam_getCamParameter(camera, KEY_PARAM_PREVIEW_FRAMERATE);
    const char* video_sizes  = DroidCam_getCamParameter(camera, KEY_PARAM_VIDEO_SIZES_LIST);
    const char* rates        = DroidCam_getCamParameter(camera, KEY_PARAM_PREVIEW_RATES_LIST);

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
    // YUV420SemiPlanar
    *format = SDL_PIXELFORMAT_NV21;
    *colorspace = SDL_COLORSPACE_YUV_DEFAULT;
    return; // FIXME: unfix

    // YUV420Planar
    //*format = SDL_PIXELFORMAT_YV21;
    //*colorspace = SDL_COLORSPACE_YUV_DEFAULT;
    //return; // FIXME: unfix

    SDL_PixelFormat pxf = SDL_PIXELFORMAT_UNKNOWN;
    SDL_Colorspace csp  = SDL_COLORSPACE_UNKNOWN;
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
    } else if(fmt == 0x7F000789) { pxf = SDL_PIXELFORMAT_EXTERNAL_OES; csp = SDL_COLORSPACE_YUV_DEFAULT;
    } else {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "Did not find format, returning default.");
        pxf = SDL_PIXELFORMAT_YV12; csp  = SDL_COLORSPACE_YUV_DEFAULT;
    }
/* from DroidMediaColourFormatConstants
    QOMX_COLOR_FormatYUV420PackedSemiPlanar32m;
    QOMX_COLOR_FormatYUV420PackedSemiPlanar64x32Tile2m8ka;
    OMX_COLOR_FormatYUV420Planar;
    OMX_COLOR_FormatYUV420PackedPlanar;
    OMX_COLOR_FormatYUV420SemiPlanar;
    OMX_COLOR_FormatYUV422SemiPlanar;
    OMX_COLOR_FormatL8;
    OMX_COLOR_FormatYCbYCr;
    OMX_COLOR_FormatYCrYCb;
    OMX_COLOR_FormatCbYCrY;
    OMX_COLOR_Format32bitARGB8888;
    OMX_COLOR_Format32bitBGRA8888;
    OMX_COLOR_Format16bitRGB565;
    OMX_COLOR_Format16bitBGR565;
    OMX_COLOR_FormatYUV420Flexible;
*/
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
/*
    {
        DroidMediaBufferQueueCallbacks cb;
        DroidMediaBufferQueue* queue = droid_media_camera_get_buffer_queue (device->hidden->droidcam);

        cb.buffers_released = DroidCam_handleBuffersReleased;
        cb.frame_available = DroidCam_handleBufferFrame;
        cb.buffer_created = DroidCam_handleBufferCreated;

        droid_media_buffer_queue_set_callbacks (queue, &cb, device);
    }
*/
}

static void DroidCam_handleError(void* data, int error) {
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleError: %d", error);
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
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handlePreviewMeta not implemented");
}

static void DroidCam_handleVideoFrame(void *data, DroidMediaCameraRecordingData *mem)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleVideoFrame not implemented");
}

static void DroidCam_handlePostviewFrame(void *data, DroidMediaData *mem)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: << handlePostviewFrame");
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: delegating to other handler:");
    DroidCam_handlePreviewFrame(data, mem);
}

static void DroidCam_handlePreviewFrame(void *data, DroidMediaData *mem)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handlePreviewFrame");

    SDL_Camera* device = (SDL_Camera*)data;

    static int skipped = 0;

    if(device->hidden->frameReady == true) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: last frame not processed, skipping");
        skipped+=1;
        if (skipped > 1000) {
            SDL_LogError(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: 1000 unprocessed frames, disabling fame callbacks.");
            DroidCam_setPreviewCallbacksEnabled(device, false);
        }
    } else {
        skipped -= 1;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: copying raw data");
        DroidCam_setPreviewCallbacksEnabled(device, false);
        device->hidden->frame->rawdata = SDL_malloc(mem->size);
        SDL_memcpy(device->hidden->frame->rawdata, mem->data, mem->size);
        device->hidden->frame->rawsize = mem->size;

        int width = device->actual_spec.width;
        int height = device->actual_spec.height;
        SDL_PixelFormat format = device->actual_spec.format;
        size_t pitch;
        if(!SDL_CalculateYUVSize(SDL_PIXELFORMAT_NV21, width, height, NULL, &pitch));

        // DroidMediaBufferInfo is actually for buffer callbacks.
        // But to be able to use the struct from both, fake it here:
        if (device->hidden->frame->info == NULL) {
            device->hidden->frame->info = (DroidMediaBufferInfo*) SDL_calloc(1, sizeof (DroidMediaBufferInfo));
        }
        device->hidden->frame->info->width  = width;
        device->hidden->frame->info->height = height;
        device->hidden->frame->info->stride = pitch;
        device->hidden->frame->info->timestamp = SDL_GetTicksNS(); // oh well, close enough.
        device->hidden->frame->info->frame_number += 1;
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handlePreviewFrame: prepared frame %ld, p %ld, s %ld, fmt %s ",
                                              device->hidden->frame->info->frame_number,
                                              pitch,
                                              mem->size,
                                              SDL_GetPixelFormatName(format));
        device->hidden->frameReady = true;

        return;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: << handlePreviewFrame");
}

/* a taken picture, jpeg format */
static void DroidCam_handleCompressedImage(void *data, DroidMediaData *mem)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleCompressedImage not implemented");
}

static void DroidCam_handleRawImage(void *data, DroidMediaData *mem) {
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleRawImage not implemented");
LOCAL_UNUSED(data);
LOCAL_UNUSED(mem);
}

static void DroidCam_handleBuffersReleased(void *data)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferReleased");
LOCAL_UNUSED(data);
}

static bool DroidCam_handleBufferCreated(void *data, DroidMediaBuffer *buf)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(buf);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferCreate");
    return true;
}

static bool DroidCam_handleBufferFrame(void *data, DroidMediaBuffer *buf)
{
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleBufferFrame");
    SDL_Camera* device = (SDL_Camera*) data;

    device->hidden->frame->info = (DroidMediaBufferInfo*) SDL_calloc(1, sizeof (DroidMediaBufferInfo));

    droid_media_buffer_lock(buf, DROID_MEDIA_BUFFER_LOCK_READ);
    droid_media_buffer_get_info(buf, device->hidden->frame->info);
    DroidMediaBufferInfo *info = device->hidden->frame->info;

    SDL_PixelFormat pfx; SDL_Colorspace csp;
    DroidCam_camFormatToSDLFormats(info->format, &pfx, &csp);

    if (device->hidden->frame->data != NULL) {
        SDL_DestroySurface(device->hidden->frame->data);
        device->hidden->frame->data = NULL;
    }
    /*
    device->hidden->frame->data = SDL_CreateSurfaceFrom(info->width, info->height,
                                             SDL_PIXELFORMAT_NV21,
                                             buf,
                                             pitch);
    */
    size_t spitch, ssize;
    SDL_CalculateYUVSize(pfx, info->width, info->height, &ssize, &spitch);
    size_t dpitch, dsize;
    SDL_CalculateYUVSize(SDL_PIXELFORMAT_RGBA32, info->width, info->height, &dsize, &dpitch);
    device->hidden->frame->data = SDL_CreateSurface(info->width, info->height, SDL_PIXELFORMAT_RGBA32);
    SDL_ConvertPixels_YUV_to_RGB(info->width, info->height,
                                  pfx, csp,
                                  0, buf, spitch,
                                  SDL_PIXELFORMAT_RGBA32, SDL_COLORSPACE_SRGB,
                                  0, device->hidden->frame->data->pixels, dpitch);

    if (device->hidden->frame->data == NULL) {
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: handleBufferFrame: Surface creation failed: %s",
                                              SDL_GetError());
        return false;
    } else {
        SDL_srand(info->timestamp);
        SDL_FillSurfaceRect(device->hidden->frame->data->pixels, NULL,
                            SDL_MapSurfaceRGBA(device->hidden->frame->data->pixels,
                                               SDL_rand(255), SDL_rand(255), SDL_rand(255), 255)
                            );
        SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM,
                     "DROIDCAMERA: handleBufferFrame: Created a surface for frame: N: %lu, t[ns]: %lu, %dx%d/%d, p: %lu",
                     info->frame_number, info->timestamp, info->width, info->height, info->stride, spitch);
        device->hidden->frameReady = true;
    }

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
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleRawImageNotify not implemented");

LOCAL_UNUSED(data);
}
static void DroidCam_handleShutter(void* data)
{
LOCAL_UNUSED(data);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleShutter not implemented");
}
static void DroidCam_handleFocus(void* data, int num)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleFocus not implemented");
}
static void DroidCam_handleFocusMove(void* data, int num)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleFocusMove not implemented");
}
static void DroidCam_handleZoom(void* data, int num1, int num2)
{
LOCAL_UNUSED(data);
LOCAL_UNUSED(num1);
LOCAL_UNUSED(num2);
    SDL_LogDebug(SDL_LOG_CATEGORY_SYSTEM, "DROIDCAMERA: >> handleZoom not implemented");
}

#endif  // SDL_CAMERA_DRIVER_DROIDMEDIA
