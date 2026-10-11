// Copyright 2026 Sylar.soluciones. SPDX-License-Identifier: MIT
// Native playable game with default-off live DK and Diddy sprite relief.
#include <jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "closed_relief.h"
#include "game_bridge.h"
#include "status_screen.h"
#include "maquette_math.h"

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"SylarQuest",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"SylarQuest",__VA_ARGS__)
enum { EYES=2 };
typedef struct Eye {
 XrSwapchain swapchain;XrSwapchainImageOpenGLESKHR *images;uint32_t count,width,height;SylarRelief renderer;
} Eye;
typedef struct ScreenApp {
    struct android_app *android;
    bool resumed, initialized, running, closing, finish_sent, xr_unusable;
    XrInstance instance;
    XrSystemId system;
    XrSession session;
    XrSessionState state;
    XrSpace local_space;
    Eye eyes[EYES];
    XrView views[EYES];
    EGLDisplay display;
    EGLConfig config;
    EGLContext context;
    EGLSurface surface;
    XrActionSet controls;XrAction stick,jump,roll,select,start,toggle_depth,toggle_filter,toggle_reconstruction;
    SylarGame *game;SylarFrame content;bool relief,smooth,reconstruction,reconstruction_trigger_down,logged_stereo,logged_game;
} ScreenApp;

static bool xr_ok(ScreenApp *s, XrResult result, const char *operation) {
    if (XR_SUCCEEDED(result) && result!=XR_SESSION_LOSS_PENDING) return true;
    char message[XR_MAX_RESULT_STRING_SIZE]="";
    if (s->instance && result!=XR_ERROR_INSTANCE_LOST)
        (void)xrResultToString(s->instance,result,message);
    LOGE("%s: %d %s",operation,(int)result,message);
    s->closing=true;
    if (result==XR_ERROR_SESSION_LOST || result==XR_ERROR_INSTANCE_LOST || result==XR_SESSION_LOSS_PENDING)
        s->xr_unusable=true;
    return false;
}
#define XR_OK(s, call) xr_ok((s),(call),#call)
static bool gl_ok(ScreenApp *s, const char *operation) {
    const GLenum error=glGetError();
    if (error==GL_NO_ERROR) return true;
    LOGE("%s: GL error 0x%x",operation,error);
    s->closing=true;
    return false;
}
static bool fail(ScreenApp *s, const char *message) {
    LOGE("%s",message); s->closing=true; return false;
}
static void on_command(struct android_app *app, int32_t command) {
    ScreenApp *s=app->userData;
    if (command==APP_CMD_RESUME) s->resumed=true;
    if (command==APP_CMD_PAUSE) {s->resumed=false;sylar_game_state(s->game,false,0);}
    // XR owns its swapchains; no Android window pointer is retained by EGL.
    if (command==APP_CMD_TERM_WINDOW) LOGI("Android surface removed; submit no layers until available.");
}
static bool extensions_available(ScreenApp *s, const char **required, size_t count) {
    uint32_t available=0;
    if (!XR_OK(s,xrEnumerateInstanceExtensionProperties(NULL,0,&available,NULL))) return false;
    XrExtensionProperties *items=calloc(available,sizeof(*items));
    if (!items) return fail(s,"Cannot allocate extension inventory.");
    for (uint32_t i=0;i<available;++i) items[i].type=XR_TYPE_EXTENSION_PROPERTIES;
    bool ok=XR_OK(s,xrEnumerateInstanceExtensionProperties(NULL,available,&available,items));
    if (ok) for (size_t j=0;j<count;++j) {
        bool found=false;
        for (uint32_t i=0;i<available;++i) if (!strcmp(required[j],items[i].extensionName)) found=true;
        if (!found) { LOGE("Required extension absent: %s",required[j]); ok=false; s->closing=true; }
    }
    free(items); return ok;
}
static bool create_egl(ScreenApp *s, const XrGraphicsRequirementsOpenGLESKHR *requirements) {
    s->display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (s->display==EGL_NO_DISPLAY || !eglInitialize(s->display,NULL,NULL)) return fail(s,"EGL initialization failed.");
    if (!eglBindAPI(EGL_OPENGL_ES_API)) return fail(s,"EGL could not bind OpenGL ES.");
    const EGLint attributes[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,
        EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,0,EGL_STENCIL_SIZE,0,EGL_NONE};
    EGLint count=0;
    if (!eglChooseConfig(s->display,attributes,&s->config,1,&count) || count!=1) return fail(s,"EGL config unavailable.");
    const EGLint context_attributes[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    const EGLint surface_attributes[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
    s->context=eglCreateContext(s->display,s->config,EGL_NO_CONTEXT,context_attributes);
    s->surface=eglCreatePbufferSurface(s->display,s->config,surface_attributes);
    if (s->context==EGL_NO_CONTEXT || s->surface==EGL_NO_SURFACE ||
        !eglMakeCurrent(s->display,s->surface,s->surface,s->context)) return fail(s,"EGL context/surface creation failed.");
    GLint major=0,minor=0;
    glGetIntegerv(GL_MAJOR_VERSION,&major); glGetIntegerv(GL_MINOR_VERSION,&minor);
    const XrVersion version=XR_MAKE_VERSION(major,minor,0);
    LOGI("OpenGL ES %d.%d; renderer: %s",major,minor,(const char *)glGetString(GL_RENDERER));
    if (version<requirements->minApiVersionSupported || version>requirements->maxApiVersionSupported)
        return fail(s,"GLES context outside runtime graphics requirements.");
    return gl_ok(s,"EGL setup");
}
static bool check_blend_mode(ScreenApp *s) {
    uint32_t count=0;
    if (!XR_OK(s,xrEnumerateEnvironmentBlendModes(s->instance,s->system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,NULL))) return false;
    XrEnvironmentBlendMode *modes=calloc(count,sizeof(*modes));
    if (!modes) return fail(s,"Cannot allocate blend-mode inventory.");
    bool ok=XR_OK(s,xrEnumerateEnvironmentBlendModes(s->instance,s->system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,count,&count,modes));
    bool opaque=false;
    if (ok) for (uint32_t i=0;i<count;++i) if (modes[i]==XR_ENVIRONMENT_BLEND_MODE_OPAQUE) opaque=true;
    free(modes);
    return ok && (opaque || fail(s,"Opaque stereo composition unsupported."));
}
static bool create_swapchain(ScreenApp *s) {
    uint32_t count=0;
    if(!XR_OK(s,xrEnumerateViewConfigurationViews(s->instance,s->system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,0,&count,NULL)))return false;
    if(count!=EYES)return fail(s,"Exactly two stereo views required.");
    XrViewConfigurationView specs[EYES]={{.type=XR_TYPE_VIEW_CONFIGURATION_VIEW},{.type=XR_TYPE_VIEW_CONFIGURATION_VIEW}};
    if(!XR_OK(s,xrEnumerateViewConfigurationViews(s->instance,s->system,XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,EYES,&count,specs)))return false;
    if(!XR_OK(s,xrEnumerateSwapchainFormats(s->session,0,&count,NULL)))return false;
    int64_t *formats=calloc(count,sizeof(*formats));if(!formats)return fail(s,"Format allocation failed.");
    bool ok=XR_OK(s,xrEnumerateSwapchainFormats(s->session,count,&count,formats));int64_t format=0;
    if(ok)for(uint32_t i=0;i<count;i++)if(formats[i]==GL_RGBA8)format=GL_RGBA8;
    if(ok)for(uint32_t i=0;i<count;i++)if(formats[i]==GL_SRGB8_ALPHA8)format=GL_SRGB8_ALPHA8;
    free(formats);if(!ok || !format)return fail(s,"RGBA/sRGB format unavailable.");
    for(unsigned eye=0;eye<EYES;eye++){
        Eye *e=&s->eyes[eye];e->width=specs[eye].recommendedImageRectWidth;e->height=specs[eye].recommendedImageRectHeight;
        if(!e->width || !e->height)return fail(s,"Invalid recommended view extent.");
        XrSwapchainCreateInfo info={.type=XR_TYPE_SWAPCHAIN_CREATE_INFO,.usageFlags=XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT,
            .format=format,.sampleCount=1,.width=e->width,.height=e->height,.faceCount=1,.arraySize=1,.mipCount=1};
        if(!XR_OK(s,xrCreateSwapchain(s->session,&info,&e->swapchain)))return false;
        if(!XR_OK(s,xrEnumerateSwapchainImages(e->swapchain,0,&e->count,NULL)) || !e->count)return false;
        e->images=calloc(e->count,sizeof(*e->images));if(!e->images)return fail(s,"Eye image allocation failed.");
        for(uint32_t i=0;i<e->count;i++)e->images[i].type=XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_ES_KHR;
        if(!XR_OK(s,xrEnumerateSwapchainImages(e->swapchain,e->count,&e->count,(XrSwapchainImageBaseHeader *)e->images)))return false;
        if((eye==0 || e->width!=s->eyes[0].width || e->height!=s->eyes[0].height) && !sylar_relief_init(&e->renderer,e->width,e->height,(GLenum)format))return fail(s,"3D renderer failed.");
        s->views[eye].type=XR_TYPE_VIEW;
    }
    return true;
}
static bool create_controls(ScreenApp *s) {
    XrActionSetCreateInfo set={.type=XR_TYPE_ACTION_SET_CREATE_INFO};
    strcpy(set.actionSetName,"game"); strcpy(set.localizedActionSetName,"DKC1 Controls");
    if (!XR_OK(s,xrCreateActionSet(s->instance,&set,&s->controls))) return false;
    const char *names[]={"move","jump","roll","select","start","toggle_depth","toggle_filter","toggle_reconstruction"};
    const char *labels[]={"Move","Jump","Run and roll","Switch Kong","Start","Sprite relief or flat","Smooth or original pixels","Reconstruct or previous scaling"};
    XrAction *actions[]={&s->stick,&s->jump,&s->roll,&s->select,&s->start,&s->toggle_depth,&s->toggle_filter,&s->toggle_reconstruction};
    const char *paths[]={"/user/hand/left/input/thumbstick","/user/hand/right/input/a/click",
        "/user/hand/right/input/b/click","/user/hand/left/input/x/click","/user/hand/left/input/y/click",
        "/user/hand/left/input/thumbstick/click","/user/hand/right/input/thumbstick/click","/user/hand/right/input/trigger/value"};
    XrActionSuggestedBinding bindings[8];
    for (int i=0;i<8;i++) {
        XrActionCreateInfo action={.type=XR_TYPE_ACTION_CREATE_INFO,
            .actionType=i==0?XR_ACTION_TYPE_VECTOR2F_INPUT:i==7?XR_ACTION_TYPE_FLOAT_INPUT:XR_ACTION_TYPE_BOOLEAN_INPUT};
        strcpy(action.actionName,names[i]); strcpy(action.localizedActionName,labels[i]);
        if (!XR_OK(s,xrCreateAction(s->controls,&action,actions[i]))) return false;
        bindings[i].action=*actions[i];
        if (!XR_OK(s,xrStringToPath(s->instance,paths[i],&bindings[i].binding))) return false;
    }
    XrPath profile;
    if (!XR_OK(s,xrStringToPath(s->instance,"/interaction_profiles/oculus/touch_controller",&profile))) return false;
    XrInteractionProfileSuggestedBinding suggest={.type=XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING,
        .interactionProfile=profile,.countSuggestedBindings=8,.suggestedBindings=bindings};
    if (!XR_OK(s,xrSuggestInteractionProfileBindings(s->instance,&suggest))) return false;
    XrSessionActionSetsAttachInfo attach={.type=XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO,
        .countActionSets=1,.actionSets=&s->controls};
    return XR_OK(s,xrAttachSessionActionSets(s->session,&attach));
}
static bool read_button(ScreenApp *s,XrAction action) {
    XrActionStateGetInfo info={.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=action};
    XrActionStateBoolean value={.type=XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_OK(s,xrGetActionStateBoolean(s->session,&info,&value)) && value.isActive && value.currentState;
}
static bool clicked(ScreenApp *s,XrAction action) {
    XrActionStateGetInfo info={.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=action};
    XrActionStateBoolean value={.type=XR_TYPE_ACTION_STATE_BOOLEAN};
    return XR_OK(s,xrGetActionStateBoolean(s->session,&info,&value)) && value.isActive && value.currentState && value.changedSinceLastSync;
}
static void update_controls(ScreenApp *s,bool should_render) {
    bool active=should_render && s->running && s->resumed && s->android->window &&
        s->state==XR_SESSION_STATE_FOCUSED && !s->closing;
    uint32_t inputs=0;
    if (active) {
        XrActiveActionSet set={.actionSet=s->controls};
        XrActionsSyncInfo sync={.type=XR_TYPE_ACTIONS_SYNC_INFO,.countActiveActionSets=1,.activeActionSets=&set};
        XrResult result=xrSyncActions(s->session,&sync);
        if (result==XR_SESSION_NOT_FOCUSED) active=false;
        else if (!xr_ok(s,result,"xrSyncActions")) active=false;
        else {
            if(clicked(s,s->toggle_depth)) {
                s->relief=!s->relief; sylar_game_relief(s->game,s->relief);
                LOGI("Presentation: %s",s->relief?"sprite relief":"flat");
            }
            if(clicked(s,s->toggle_filter)) {
                s->smooth=!s->smooth; LOGI("Filter: %s",s->smooth?"EPX and smooth":"original pixels");
            }
            XrActionStateGetInfo reconstruction_info={.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=s->toggle_reconstruction};XrActionStateFloat trigger={.type=XR_TYPE_ACTION_STATE_FLOAT};
            if(XR_OK(s,xrGetActionStateFloat(s->session,&reconstruction_info,&trigger))){bool pressed=trigger.isActive && trigger.currentState>.75f;if(pressed && !s->reconstruction_trigger_down){s->reconstruction=!s->reconstruction;LOGI("Scaling: %s",s->reconstruction?"Reconstruct":"previous EPX4");}s->reconstruction_trigger_down=pressed;}
            XrActionStateGetInfo info={.type=XR_TYPE_ACTION_STATE_GET_INFO,.action=s->stick};
            XrActionStateVector2f stick={.type=XR_TYPE_ACTION_STATE_VECTOR2F};
            if (!XR_OK(s,xrGetActionStateVector2f(s->session,&info,&stick))) active=false;
            else inputs=sylar_touch_mask(stick.isActive?stick.currentState.x:0,
                stick.isActive?stick.currentState.y:0,read_button(s,s->jump),read_button(s,s->roll),
                read_button(s,s->select),read_button(s,s->start));
        }
    }
    if(!active)s->reconstruction_trigger_down=false;
    sylar_game_state(s->game,active && !s->closing,inputs);
}
static bool initialize(ScreenApp *s) {
    PFN_xrInitializeLoaderKHR init_loader=NULL;
    if (!XR_OK(s,xrGetInstanceProcAddr(XR_NULL_HANDLE,"xrInitializeLoaderKHR",(PFN_xrVoidFunction *)&init_loader))) return false;
    if (!init_loader) return fail(s,"Android loader initialization entrypoint missing.");
    XrLoaderInitInfoAndroidKHR loader={.type=XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR,
        .applicationVM=s->android->activity->vm,.applicationContext=s->android->activity->clazz};
    if (!XR_OK(s,init_loader((const XrLoaderInitInfoBaseHeaderKHR *)&loader))) return false;
    const char *required[]={XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME};
    if (!extensions_available(s,required,2)) return false;
    XrInstanceCreateInfoAndroidKHR android_info={.type=XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR,
        .applicationVM=s->android->activity->vm,.applicationActivity=s->android->activity->clazz};
    XrInstanceCreateInfo instance_info={.type=XR_TYPE_INSTANCE_CREATE_INFO,.next=&android_info,.enabledExtensionCount=2,.enabledExtensionNames=required};
    strcpy(instance_info.applicationInfo.applicationName,"DKC1 Quest VR");
    strcpy(instance_info.applicationInfo.engineName,"Sylar.soluciones");
    instance_info.applicationInfo.applicationVersion=1;
    instance_info.applicationInfo.engineVersion=1;
    instance_info.applicationInfo.apiVersion=XR_MAKE_VERSION(1,0,0);
    if (!XR_OK(s,xrCreateInstance(&instance_info,&s->instance))) return false;
    XrInstanceProperties instance_properties={.type=XR_TYPE_INSTANCE_PROPERTIES};
    if (!XR_OK(s,xrGetInstanceProperties(s->instance,&instance_properties))) return false;
    LOGI("OpenXR runtime: %s",instance_properties.runtimeName);
    XrSystemGetInfo system_info={.type=XR_TYPE_SYSTEM_GET_INFO,.formFactor=XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY};
    if (!XR_OK(s,xrGetSystem(s->instance,&system_info,&s->system))) return false;
    XrSystemProperties properties={.type=XR_TYPE_SYSTEM_PROPERTIES};
    if (!XR_OK(s,xrGetSystemProperties(s->instance,s->system,&properties))) return false;
    if(properties.graphicsProperties.maxLayerCount<1)return fail(s,"Projection layer unsupported.");
    if (!check_blend_mode(s)) return false;
    PFN_xrGetOpenGLESGraphicsRequirementsKHR get_requirements=NULL;
    if (!XR_OK(s,xrGetInstanceProcAddr(s->instance,"xrGetOpenGLESGraphicsRequirementsKHR",(PFN_xrVoidFunction *)&get_requirements))) return false;
    if (!get_requirements) return fail(s,"GLES graphics requirements entrypoint missing.");
    XrGraphicsRequirementsOpenGLESKHR requirements={.type=XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    if (!XR_OK(s,get_requirements(s->instance,s->system,&requirements)) || !create_egl(s,&requirements)) return false;
    XrGraphicsBindingOpenGLESAndroidKHR graphics={.type=XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR,
        .display=s->display,.config=s->config,.context=s->context};
    XrSessionCreateInfo session_info={.type=XR_TYPE_SESSION_CREATE_INFO,.next=&graphics,.systemId=s->system};
    if (!XR_OK(s,xrCreateSession(s->instance,&session_info,&s->session))) return false;
    XrReferenceSpaceCreateInfo space_info={.type=XR_TYPE_REFERENCE_SPACE_CREATE_INFO,.referenceSpaceType=XR_REFERENCE_SPACE_TYPE_LOCAL,
        .poseInReferenceSpace={.orientation={0,0,0,1}}};
    if (!XR_OK(s,xrCreateReferenceSpace(s->session,&space_info,&s->local_space))) return false;
    if (!create_controls(s) || !create_swapchain(s)) return false;
    s->game=sylar_game_start(s->android->activity->internalDataPath);
    if(!s->game)return fail(s,"Cannot attach private game worker.");
    char option_path[1024];int length=snprintf(option_path,sizeof(option_path),"%s/relief-gameplay.enabled",s->android->activity->internalDataPath);
    if(length>0 && (size_t)length<sizeof(option_path)){FILE *option=fopen(option_path,"rb");if(option){s->relief=fgetc(option)=='1';fclose(option);}}
    length=snprintf(option_path,sizeof(option_path),"%s/reconstruction.enabled",s->android->activity->internalDataPath);
    if(length>0 && (size_t)length<sizeof(option_path)){FILE *option=fopen(option_path,"rb");if(option){s->reconstruction=fgetc(option)=='1';fclose(option);}}
    s->smooth=true;sylar_game_diorama(s->game,true);sylar_game_relief(s->game,s->relief);
    s->initialized=true;
    LOGI("Reconstruction 0.8.2 ready: relief=%d, reconstruction=%d, 16:9 diorama 3.407 x 1.913m, full eye resolution, Reconstruct2 + EPX2; normal terrain; right trigger compares Reconstruct/EPX4.",s->relief,s->reconstruction);
    return true;
}
static void poll_xr(ScreenApp *s) {
    if (!s->instance || s->xr_unusable) return;
    for (int i=0;i<64;++i) {
        XrEventDataBuffer buffer={.type=XR_TYPE_EVENT_DATA_BUFFER};
        XrResult result=xrPollEvent(s->instance,&buffer);
        if (result==XR_EVENT_UNAVAILABLE) return;
        if (!xr_ok(s,result,"xrPollEvent")) return;
        if (buffer.type==XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            s->closing=true; s->xr_unusable=true; return;
        }
        if (buffer.type==XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING) LOGI("Reference-space recenter/change queued by runtime.");
        if (buffer.type!=XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) continue;
        const XrEventDataSessionStateChanged *event=(const XrEventDataSessionStateChanged *)&buffer;
        if (event->session!=s->session) continue;
        s->state=event->state;
        if(s->state!=XR_SESSION_STATE_FOCUSED)sylar_game_state(s->game,false,0);
        LOGI("XR session state: %d",(int)s->state);
        if (s->state==XR_SESSION_STATE_READY) {
            XrSessionBeginInfo begin={.type=XR_TYPE_SESSION_BEGIN_INFO,.primaryViewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
            if (XR_OK(s,xrBeginSession(s->session,&begin))) s->running=true;
        } else if (s->state==XR_SESSION_STATE_STOPPING) {
            if (s->running) (void)XR_OK(s,xrEndSession(s->session));
            s->running=false;
        } else if (s->state==XR_SESSION_STATE_EXITING || s->state==XR_SESSION_STATE_LOSS_PENDING) {
            s->running=false; s->closing=true;
            if (s->state==XR_SESSION_STATE_LOSS_PENDING) s->xr_unusable=true;
        }
    }
}
static void frame(ScreenApp *s){
    XrFrameWaitInfo wait={.type=XR_TYPE_FRAME_WAIT_INFO};XrFrameState state={.type=XR_TYPE_FRAME_STATE};
    if(!XR_OK(s,xrWaitFrame(s->session,&wait,&state)))return;
    XrFrameBeginInfo begin={.type=XR_TYPE_FRAME_BEGIN_INFO};if(!XR_OK(s,xrBeginFrame(s->session,&begin)))return;
    update_controls(s,state.shouldRender);
    enum SylarGameStatus status=sylar_game_frame(s->game,&s->content);
    if(status!=SYLAR_READY){sylar_status_screen(s->content.rgba,status);s->content.layers_valid=false;s->content.relief_valid=false;s->content.sequence=(uint64_t)state.predictedDisplayTime;}
    else if(!s->logged_game){LOGI("Native game image ready; audio and input owned by preserved engine worker.");s->logged_game=true;}
    bool published=false;XrCompositionLayerProjectionView projection[EYES];memset(projection,0,sizeof(projection));
    if(state.shouldRender && s->resumed && s->android->window && !s->closing){
        XrViewLocateInfo locate={.type=XR_TYPE_VIEW_LOCATE_INFO,.viewConfigurationType=XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
            .displayTime=state.predictedDisplayTime,.space=s->local_space};
        XrViewState view_state={.type=XR_TYPE_VIEW_STATE};uint32_t count=0;
        bool ok=XR_OK(s,xrLocateViews(s->session,&locate,&view_state,EYES,&count,s->views));
        const XrViewStateFlags required=XR_VIEW_STATE_POSITION_VALID_BIT|XR_VIEW_STATE_ORIENTATION_VALID_BIT;
        if(ok && count==EYES && (view_state.viewStateFlags&required)==required){
            published=true;
            for(unsigned eye=0;eye<EYES && !s->xr_unusable;eye++){
                Eye *e=&s->eyes[eye];XrView *view=&s->views[eye];float p[16],v[16],vp[16];
                const float t[3]={view->pose.position.x,view->pose.position.y,view->pose.position.z};
                const float q[4]={view->pose.orientation.x,view->pose.orientation.y,view->pose.orientation.z,view->pose.orientation.w};
                if(!sylar_projection(p,view->fov.angleLeft,view->fov.angleRight,view->fov.angleDown,view->fov.angleUp,.05f,20.f)){
                    (void)fail(s,"Invalid eye projection.");published=false;break;}
                sylar_view(v,t,q);sylar_multiply(vp,p,v);
                uint32_t index=0;XrSwapchainImageAcquireInfo acquire={.type=XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO};
                if(!XR_OK(s,xrAcquireSwapchainImage(e->swapchain,&acquire,&index))){published=false;break;}
                XrSwapchainImageWaitInfo image_wait={.type=XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO,.timeout=XR_INFINITE_DURATION};
                XrResult ready=xrWaitSwapchainImage(e->swapchain,&image_wait);
                if(ready!=XR_SUCCESS){
                    if(ready==XR_TIMEOUT_EXPIRED)(void)fail(s,"Infinite-wait image timeout.");else (void)xr_ok(s,ready,"xrWaitSwapchainImage");
                    published=false;break;
                }
                sylar_relief_reconstruction(e->width==s->eyes[0].width && e->height==s->eyes[0].height?&s->eyes[0].renderer:&e->renderer,s->reconstruction);
                bool drawn=index<e->count && sylar_relief_draw(e->width==s->eyes[0].width && e->height==s->eyes[0].height?&s->eyes[0].renderer:&e->renderer,e->images[index].image,vp,&s->content,s->relief,s->smooth);
                XrSwapchainImageReleaseInfo release={.type=XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO};
                bool released=XR_OK(s,xrReleaseSwapchainImage(e->swapchain,&release));
                if(!drawn)(void)fail(s,"3D eye render failed.");published=published && drawn && released;
                projection[eye]=(XrCompositionLayerProjectionView){.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW,
                    .pose=view->pose,.fov=view->fov,.subImage={.swapchain=e->swapchain,.imageRect={.offset={0,0},
                    .extent={(int32_t)e->width,(int32_t)e->height}},.imageArrayIndex=0}};
            }
            if(published && !s->logged_stereo){
                LOGI("Stereo 3D projection submitted: L x=%.4f R x=%.4f; independent eye poses and depth-tested meshes.",
                     s->views[0].pose.position.x,s->views[1].pose.position.x);s->logged_stereo=true;
            }
        }
    }
    if(s->xr_unusable)return;
    XrCompositionLayerProjection layer={.type=XR_TYPE_COMPOSITION_LAYER_PROJECTION,.space=s->local_space,.viewCount=EYES,.views=projection};
    const XrCompositionLayerBaseHeader *layers[]={(const XrCompositionLayerBaseHeader *)&layer};
    XrFrameEndInfo end={.type=XR_TYPE_FRAME_END_INFO,.displayTime=state.predictedDisplayTime,.environmentBlendMode=XR_ENVIRONMENT_BLEND_MODE_OPAQUE,
        .layerCount=published?1u:0u,.layers=published?layers:NULL};(void)XR_OK(s,xrEndFrame(s->session,&end));
}
static void shutdown(ScreenApp *s) {
    sylar_game_detach(s->game);s->game=NULL;
    if (s->display!=EGL_NO_DISPLAY && s->context!=EGL_NO_CONTEXT && s->surface!=EGL_NO_SURFACE) {
        if (eglMakeCurrent(s->display,s->surface,s->surface,s->context)) {
            for(unsigned eye=0;eye<EYES;eye++)sylar_relief_destroy(&s->eyes[eye].renderer);
        }
    }
    for(unsigned eye=0;eye<EYES;eye++){if(s->eyes[eye].swapchain)(void)xrDestroySwapchain(s->eyes[eye].swapchain);free(s->eyes[eye].images);}
    if (s->local_space) (void)xrDestroySpace(s->local_space);
    if (s->session) (void)xrDestroySession(s->session);
    if (s->controls) (void)xrDestroyActionSet(s->controls);
    if (s->instance) (void)xrDestroyInstance(s->instance);
    if (s->display!=EGL_NO_DISPLAY) {
        (void)eglMakeCurrent(s->display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
        if (s->surface!=EGL_NO_SURFACE) (void)eglDestroySurface(s->display,s->surface);
        if (s->context!=EGL_NO_CONTEXT) (void)eglDestroyContext(s->display,s->context);
        (void)eglTerminate(s->display);
    }
    LOGI("Native sprite relief resources released.");
}
void android_main(struct android_app *app) {
    ScreenApp *s=calloc(1,sizeof(*s));
    if (!s) { ANativeActivity_finish(app->activity); return; }
    s->android=app; s->display=EGL_NO_DISPLAY; s->context=EGL_NO_CONTEXT; s->surface=EGL_NO_SURFACE;
    app->userData=s; app->onAppCmd=on_command;
    JNIEnv *jni=NULL;
    const jint env_result=(*app->activity->vm)->GetEnv(app->activity->vm,(void **)&jni,JNI_VERSION_1_6);
    bool attached=false;
    if (env_result==JNI_EDETACHED) {
        attached=(*app->activity->vm)->AttachCurrentThread(app->activity->vm,&jni,NULL)==JNI_OK;
        if (!attached) s->closing=true;
    } else if (env_result!=JNI_OK) s->closing=true;
    ANativeActivity_setWindowFlags(app->activity,AWINDOW_FLAG_KEEP_SCREEN_ON,0);
    while (!app->destroyRequested) {
        int timeout=s->running && !s->xr_unusable ? 0 : 100;
        for (int i=0;i<32 && !app->destroyRequested;++i) {
            struct android_poll_source *source=NULL;
            const int result=ALooper_pollOnce(timeout,NULL,NULL,(void **)&source);
            timeout=0;
            if (result>=0 && source) source->process(app,source);
            if (result==ALOOPER_POLL_ERROR) { s->closing=true; break; }
            if (result<0) break;
        }
        if (app->destroyRequested) break;
        if (!s->initialized && !s->closing && s->resumed && app->window) (void)initialize(s);
        poll_xr(s);
        if (s->closing && !s->finish_sent) {
            sylar_game_state(s->game,false,0);
            if (s->running && !s->xr_unusable) (void)xrRequestExitSession(s->session);
            ANativeActivity_finish(app->activity); s->finish_sent=true;
        }
        if (s->running && !s->xr_unusable) frame(s);
    }
    shutdown(s);
    app->onAppCmd=NULL; app->userData=NULL;
    if (attached) (void)(*app->activity->vm)->DetachCurrentThread(app->activity->vm);
    free(s);
}
