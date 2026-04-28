#include "droidmedia/droidmediacamera.h"
#include "droidmedia/droidmediaconstants.h"

// callbacks
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

// helpers
static CameraFormatAddData DroidCam_camParametersToSDLCaminfo(DroidMediaCamera *camera);

static bool initDroid();
static void DroidCam_setupCallbacks(SDL_Camera* device);
static void DroidCam_setPreviewCallbacksEnabled(SDL_Camera* device, bool);

static void DroidCam_fillCamParameters(SDL_Camera* device);
static char* DroidCam_getCamParameter(DroidMediaCamera* camera, const char* key);
static bool DroidCam_setCamParameter(SDL_Camera* device, const char* key, const char* value);
static bool DroidCam_commitCamParameters(SDL_Camera* device);

static SDL_CameraPosition DroidCam_camPositionToSDLPosition(int facing);
static void DroidCam_camFormatToSDLFormats(int fmt, SDL_PixelFormat *format, SDL_Colorspace *colorspace);

