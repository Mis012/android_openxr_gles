/* ------------------------------------------------ *
 * The MIT License (MIT)
 * Copyright (c) 2022 terryky1220@gmail.com
 * ------------------------------------------------ */
#include <GLES3/gl31.h>
#include "util_egl.h"
#include "util_oxr.h"


#define ENUM_CASE_STR(name, val) case name: return #name;
#define MAKE_TO_STRING_FUNC(enumType)                  \
    inline const char* to_string(enumType e) {         \
        switch (e) {                                   \
            XR_LIST_ENUM_##enumType(ENUM_CASE_STR)     \
            default: return "Unknown " #enumType;      \
        }                                              \
    }

MAKE_TO_STRING_FUNC(XrReferenceSpaceType);
MAKE_TO_STRING_FUNC(XrViewConfigurationType);
MAKE_TO_STRING_FUNC(XrEnvironmentBlendMode);
MAKE_TO_STRING_FUNC(XrSessionState);
MAKE_TO_STRING_FUNC(XrResult);
MAKE_TO_STRING_FUNC(XrFormFactor);




/* ---------------------------------------------------------------------------- *
 *  Initialize OpenXR Loader
 * ---------------------------------------------------------------------------- */
int
oxr_initialize_loader (void *appVM, void *appCtx)
{
    PFN_xrInitializeLoaderKHR xrInitializeLoaderKHR;
    xrGetInstanceProcAddr (XR_NULL_HANDLE, "xrInitializeLoaderKHR", 
                           (PFN_xrVoidFunction *)&xrInitializeLoaderKHR);

    XrLoaderInitInfoAndroidKHR info = {XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR};
    info.applicationVM      = appVM;
    info.applicationContext = appCtx;

    xrInitializeLoaderKHR ((XrLoaderInitInfoBaseHeaderKHR *)&info);

    return 0;
}


/* ---------------------------------------------------------------------------- *
 *  Create OpenXR Instance with Android/OpenGLES binding
 * ---------------------------------------------------------------------------- */
XrInstance
oxr_create_instance (void *appVM, void *appCtx)
{
    const char *ext_platform = "XR_KHR_android_create_instance";
    const char *ext_graphics = "XR_KHR_opengl_es_enable";
    const char *extensions[2] = {ext_platform, ext_graphics};

    XrInstanceCreateInfoAndroidKHR ciAndroid = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    ciAndroid.applicationVM       = appVM;
    ciAndroid.applicationActivity = appCtx;

    XrInstanceCreateInfo ci = {XR_TYPE_INSTANCE_CREATE_INFO};
    ci.next                       = &ciAndroid;
    ci.enabledExtensionCount      = 2;
    ci.enabledExtensionNames      = extensions;
    ci.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;
    strncpy (ci.applicationInfo.applicationName, "OXR_GLES_APP", XR_MAX_ENGINE_NAME_SIZE-1);

    XrInstance instance;
    xrCreateInstance (&ci, &instance);

    /* query instance name, version */
    XrInstanceProperties prop = {XR_TYPE_INSTANCE_PROPERTIES};
    xrGetInstanceProperties (instance, &prop);
    LOGI("OpenXR Instance Runtime   : \"%s\", Version: %u.%u.%u", prop.runtimeName,
            XR_VERSION_MAJOR (prop.runtimeVersion),
            XR_VERSION_MINOR (prop.runtimeVersion),
            XR_VERSION_PATCH (prop.runtimeVersion));

    return instance;
}


/* ---------------------------------------------------------------------------- *
 *  Get OpenXR Sysem
 * ---------------------------------------------------------------------------- */
XrSystemId
oxr_get_system (XrInstance instance)
{
    XrSystemGetInfo sysInfo = {XR_TYPE_SYSTEM_GET_INFO};
    sysInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrSystemId sysid;
    xrGetSystem (instance, &sysInfo, &sysid);

    /* query system properties*/
    XrSystemProperties prop = {XR_TYPE_SYSTEM_PROPERTIES};
    xrGetSystemProperties (instance, sysid, &prop);

    LOGI ("-----------------------------------------------------------------");
    LOGI ("System Properties         : Name=\"%s\", VendorId=%x", prop.systemName, prop.vendorId);
    LOGI ("System Graphics Properties: SwapchainMaxWH=(%d, %d), MaxLayers=%d",
            prop.graphicsProperties.maxSwapchainImageWidth,
            prop.graphicsProperties.maxSwapchainImageHeight,
            prop.graphicsProperties.maxLayerCount);
    LOGI ("System Tracking Properties: Orientation=%d, Position=%d",
            prop.trackingProperties.orientationTracking,
            prop.trackingProperties.positionTracking);
    LOGI ("-----------------------------------------------------------------");

    return sysid;
}


/* ---------------------------------------------------------------------------- *
 *  Confirm OpenGLES version.
 * ---------------------------------------------------------------------------- */
int
oxr_confirm_gfx_requirements (XrInstance instance, XrSystemId sysid)
{
    PFN_xrGetOpenGLESGraphicsRequirementsKHR xrGetOpenGLESGraphicsRequirementsKHR;
    xrGetInstanceProcAddr (instance, "xrGetOpenGLESGraphicsRequirementsKHR",
                           (PFN_xrVoidFunction *)&xrGetOpenGLESGraphicsRequirementsKHR);

    XrGraphicsRequirementsOpenGLESKHR gfxReq = {XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    xrGetOpenGLESGraphicsRequirementsKHR (instance, sysid, &gfxReq);


    GLint major, minor;
    glGetIntegerv (GL_MAJOR_VERSION, &major);
    glGetIntegerv (GL_MINOR_VERSION, &minor);
    XrVersion glver = XR_MAKE_VERSION (major, minor, 0);

    LOGI ("GLES version: %" PRIx64 ", supported: (%" PRIx64 " - %" PRIx64 ")\n",
          glver, gfxReq.minApiVersionSupported, gfxReq.maxApiVersionSupported);

    if (glver < gfxReq.minApiVersionSupported ||
        glver > gfxReq.maxApiVersionSupported)
    {
        LOGE ("GLES version %" PRIx64 " is not supported. (%" PRIx64 " - %" PRIx64 ")\n",
              glver, gfxReq.minApiVersionSupported, gfxReq.maxApiVersionSupported);
        return -1;
    }

    return 0;
}


/* ---------------------------------------------------------------------------- *
 *  View operation
 * ---------------------------------------------------------------------------- */
XrViewConfigurationView *
oxr_enumerate_viewconfig (XrInstance instance, XrSystemId sysid, uint32_t *numview)
{
    uint32_t                numConf;
    XrViewConfigurationView *conf;
    XrViewConfigurationType viewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    xrEnumerateViewConfigurationViews (instance, sysid, viewType, 0, &numConf, NULL);

    conf = (XrViewConfigurationView *)calloc (sizeof(XrViewConfigurationView), numConf);
    for (uint32_t i = 0; i < numConf; i ++)
        conf[i].type = XR_TYPE_VIEW_CONFIGURATION_VIEW;

    xrEnumerateViewConfigurationViews (instance, sysid, viewType, numConf, &numConf, conf);

    LOGI ("ViewConfiguration num: %d", numConf);
    for (uint32_t i = 0; i < numConf; i++)
    {
        XrViewConfigurationView &vp = conf[i];
        LOGI ("ViewConfiguration[%d/%d]: MaxWH(%d, %d), MaxSample(%d)", i, numConf,
              vp.maxImageRectWidth, vp.maxImageRectHeight, vp.maxSwapchainSampleCount);
        LOGI ("                        RecWH(%d, %d), RecSample(%d)",
              vp.recommendedImageRectWidth, vp.recommendedImageRectHeight, vp.recommendedSwapchainSampleCount);
    }

    *numview = numConf;
    return conf;
}

int
oxr_locate_views (XrSession session, XrTime dpy_time, XrSpace space, uint32_t *view_cnt, XrView *view_array)
{
    XrViewConfigurationType viewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    XrViewState viewstat = {XR_TYPE_VIEW_STATE};
    uint32_t    view_cnt_in = *view_cnt;
    uint32_t    view_cnt_out;

    XrViewLocateInfo vloc = {XR_TYPE_VIEW_LOCATE_INFO};
    vloc.viewConfigurationType = viewType;
    vloc.displayTime           = dpy_time;
    vloc.space                 = space;
    xrLocateViews (session, &vloc, &viewstat, view_cnt_in, &view_cnt_out, view_array);

    if ((viewstat.viewStateFlags & XR_VIEW_STATE_POSITION_VALID_BIT)    == 0 ||
        (viewstat.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT) == 0) {
        *view_cnt = 0;  // There is no valid tracking poses for the views.
    }
    return 0;
}




/* ---------------------------------------------------------------------------- *
 *  Swapchain operation
 * ---------------------------------------------------------------------------- */
XrSwapchain
oxr_create_swapchain (XrSession session, uint32_t width, uint32_t height)
{
    XrSwapchainCreateInfo ci = {XR_TYPE_SWAPCHAIN_CREATE_INFO};
    ci.usageFlags  = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    ci.format      = GL_RGBA8;
    ci.width       = width;
    ci.height      = height;
    ci.faceCount   = 1;
    ci.arraySize   = 1;
    ci.mipCount    = 1;
    ci.sampleCount = 1;

    XrSwapchain swapchain;
    xrCreateSwapchain (session, &ci, &swapchain);

    return swapchain;
}


XrSwapchainImageOpenGLESKHR *
oxr_alloc_swapchain_imgs (XrSwapchain swapchain)
{
    uint32_t imgCnt;
    xrEnumerateSwapchainImages (swapchain, 0, &imgCnt, NULL);
    LOGI ("SwapchainImage num: %d", imgCnt);

    XrSwapchainImageOpenGLESKHR *img_gles = (XrSwapchainImageOpenGLESKHR *)calloc(sizeof(XrSwapchainImageOpenGLESKHR), imgCnt);
    for (uint32_t i = 0; i < imgCnt; i ++)
        img_gles[i].type = XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;

    xrEnumerateSwapchainImages (swapchain, imgCnt, &imgCnt, (XrSwapchainImageBaseHeader *)img_gles);

    return img_gles;
}


int
oxr_create_swapchain (swapchain_obj_t *scobj, XrSession session, uint32_t width, uint32_t height)
{
    scobj->width     = width;
    scobj->height    = height;
    scobj->handle    = oxr_create_swapchain (session, width, height);
    scobj->img_array = oxr_alloc_swapchain_imgs (scobj->handle);

    return 0;
}

uint32_t
oxr_acquire_swapchain_img (XrSwapchain swapchain)
{
    uint32_t imgIdx;
    XrSwapchainImageAcquireInfo acquireInfo {XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
    xrAcquireSwapchainImage (swapchain, &acquireInfo, &imgIdx);

    XrSwapchainImageWaitInfo waitInfo {XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO};
    waitInfo.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage (swapchain, &waitInfo);

    return imgIdx;
}

int
oxr_acquire_swapchain_image (swapchain_obj_t *scobj, XrSwapchainImageOpenGLESKHR *glesImg, XrSwapchainSubImage *subImg)
{
    subImg->swapchain               = scobj->handle;
    subImg->imageRect.offset.x      = 0;
    subImg->imageRect.offset.y      = 0;
    subImg->imageRect.extent.width  = scobj->width;
    subImg->imageRect.extent.height = scobj->height;
    subImg->imageArrayIndex         = 0;

    uint32_t imgIdx = oxr_acquire_swapchain_img (scobj->handle);
    *glesImg = scobj->img_array[imgIdx];

    return 0;
}

void
oxr_release_swapchain_image (swapchain_obj_t *scobj)
{
    XrSwapchainImageReleaseInfo releaseInfo {XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
    xrReleaseSwapchainImage (scobj->handle, &releaseInfo);
}




/* ---------------------------------------------------------------------------- *
 *  Frame operation
 * ---------------------------------------------------------------------------- */
int
oxr_begin_frame (XrSession session, XrTime *dpy_time)
{
    XrFrameWaitInfo frameWait  = {XR_TYPE_FRAME_WAIT_INFO};
    XrFrameState    frameState = {XR_TYPE_FRAME_STATE};
    xrWaitFrame (session, &frameWait, &frameState);

    XrFrameBeginInfo frameBegin = {XR_TYPE_FRAME_BEGIN_INFO};
    xrBeginFrame (session, &frameBegin);

    *dpy_time = frameState.predictedDisplayTime;
    return (int)frameState.shouldRender;
}


int
oxr_end_frame (XrSession session, XrTime dpy_time, std::vector<XrCompositionLayerBaseHeader*> &layers)
{
    XrFrameEndInfo frameEnd {XR_TYPE_FRAME_END_INFO};
    frameEnd.displayTime          = dpy_time;
    frameEnd.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    frameEnd.layerCount           = (uint32_t)layers.size();
    frameEnd.layers               = layers.data();
    xrEndFrame (session, &frameEnd);

    return 0;
}




/* ---------------------------------------------------------------------------- *
 *  Space operation
 * ---------------------------------------------------------------------------- */
XrSpace
oxr_create_ref_space (XrSession session, XrReferenceSpaceType ref_space_type)
{
    XrSpace space;
    XrReferenceSpaceCreateInfo ci = {XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
    ci.referenceSpaceType                  = ref_space_type;
    ci.poseInReferenceSpace.orientation.w  = 1;
    xrCreateReferenceSpace (session, &ci, &space);

    return space;
}


XrSpace
oxr_create_action_space (XrSession session, XrAction action, XrPath subpath)
{
    XrSpace space;
    XrActionSpaceCreateInfo ci = {XR_TYPE_ACTION_SPACE_CREATE_INFO};
    ci.action                          = action;
    ci.poseInActionSpace.orientation.w = 1.0f;
    ci.subactionPath                   = subpath;
    xrCreateActionSpace (session, &ci, &space);

    return space;
}


XrActionSet
oxr_create_actionset (XrInstance instance, const char *name, const char *local_name, int priority)
{
    XrActionSet actionset;
    XrActionSetCreateInfo ci = {XR_TYPE_ACTION_SET_CREATE_INFO};
    ci.priority = priority;
    strcpy (ci.actionSetName,          name);
    strcpy (ci.localizedActionSetName, local_name);
    xrCreateActionSet (instance, &ci, &actionset);

    return actionset;
}


XrAction
oxr_create_action (XrActionSet actionset, XrActionType type, const char *name, const char *local_name,
                   int subpath_num, XrPath *subpath_array)
{
    XrAction action;
    XrActionCreateInfo ci = {XR_TYPE_ACTION_CREATE_INFO};
    ci.actionType          = type;
    ci.countSubactionPaths = subpath_num;
    ci.subactionPaths      = subpath_array;
    strcpy (ci.actionName,          name);
    strcpy (ci.localizedActionName, local_name);
    xrCreateAction (actionset, &ci, &action);

    return action;
}




/* ---------------------------------------------------------------------------- *
 *  Session operation
 * ---------------------------------------------------------------------------- */
static XrSessionState s_session_state   = XR_SESSION_STATE_UNKNOWN;
static bool           s_session_running = false;

XrSession
oxr_create_session (XrInstance instance, XrSystemId sysid)
{
    XrGraphicsBindingOpenGLESAndroidKHR gfxBind = {XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
    gfxBind.display = egl_get_display();
    gfxBind.config  = egl_get_config();
    gfxBind.context = egl_get_context();

    XrSessionCreateInfo ci = {XR_TYPE_SESSION_CREATE_INFO};
    ci.next     = &gfxBind;
    ci.systemId = sysid;

    XrSession session;
    xrCreateSession (instance, &ci, &session); 

    return session;
}


int
oxr_begin_session (XrSession session)
{
    XrViewConfigurationType viewType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;

    XrSessionBeginInfo bi {XR_TYPE_SESSION_BEGIN_INFO};
    bi.primaryViewConfigurationType = viewType;
    xrBeginSession (session, &bi);

    return 0;
}

int
oxr_handle_session_state_changed (XrSession session, XrEventDataSessionStateChanged &ev,
                                  bool *exitLoop, bool *reqRestart)
{
    XrSessionState old_state = s_session_state;
    XrSessionState new_state = ev.state;
    s_session_state = new_state;

    LOGI ("  [SessionState]: %s -> %s (session=%p, time=%ld)",
          to_string(old_state), to_string(new_state), ev.session, ev.time);

    if ((ev.session != XR_NULL_HANDLE) &&
        (ev.session != session))
    {
        LOGE ("XrEventDataSessionStateChanged for unknown session");
        return -1;
    }

    switch (new_state) {
    case XR_SESSION_STATE_READY:
        oxr_begin_session (session);
        s_session_running = true;
        break;

    case XR_SESSION_STATE_STOPPING:
        xrEndSession (session);
        s_session_running = false;
        break;

    case XR_SESSION_STATE_EXITING:
        *exitLoop      = true;
        *reqRestart    = false;    // Do not attempt to restart because user closed this session.
        break;

    case XR_SESSION_STATE_LOSS_PENDING:
        *exitLoop      = true;
        *reqRestart    = true;     // Poll for a new instance.
        break;

    default:
        break;
    }
    return 0;
}

bool
oxr_is_session_running ()
{
    return s_session_running;
}


static XrEventDataBuffer s_evDataBuf;

XrEventDataBaseHeader *
oxr_poll_event (XrInstance instance, XrSession session)
{
    XrEventDataBaseHeader *ev = reinterpret_cast<XrEventDataBaseHeader*>(&s_evDataBuf);
    *ev = {XR_TYPE_EVENT_DATA_BUFFER};

    XrResult xr = xrPollEvent (instance, &s_evDataBuf);
    if (xr == XR_EVENT_UNAVAILABLE)
        return nullptr;

    if (xr != XR_SUCCESS)
    {
        LOGE ("xrPollEvent");
        return NULL;
    }

    if (ev->type == XR_TYPE_EVENT_DATA_EVENTS_LOST)
    {
        XrEventDataEventsLost *evLost = reinterpret_cast<XrEventDataEventsLost*>(ev);
        LOGW ("%p events lost", evLost);
    }
    return ev;
}


int
oxr_poll_events (XrInstance instance, XrSession session, bool *exit_loop, bool *req_restart)
{
    *exit_loop   = false;
    *req_restart = false;

    // Process all pending messages.
    while (XrEventDataBaseHeader *ev = oxr_poll_event (instance, session))
    {
        switch (ev->type) {
            case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
                LOGW ("XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING");
                *exit_loop   = true;
                *req_restart = true;
                return -1;
            }

            case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
                LOGW ("XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED");
                XrEventDataSessionStateChanged sess_ev = *(XrEventDataSessionStateChanged *)ev;
                oxr_handle_session_state_changed (session, sess_ev, exit_loop, req_restart);
                break;
            }

            case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED:
                LOGW ("XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED");
                break;

            case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING:
                LOGW ("XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING");
                break;

            default:
                LOGE ("Unknown event type %d", ev->type);
                break;
        }
    }
    return 0;
}


