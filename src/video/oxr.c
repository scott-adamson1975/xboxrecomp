/*
 * oxr.c - a VR headset through OpenXR; see oxr.h. Written against OpenXR 1.1 (the Khronos headers, Apache-2.0 or MIT),
 * the loader opened at run time.
 *
 * What is here and why:
 *   - xbox_VrInit makes an XrInstance with XR_KHR_vulkan_enable2 and asks for the headset's system, then installs the
 *     core's creation hooks (nv2a_vk_core.h): the Vulkan instance, the physical device and the device are made by
 *     xrCreateVulkanInstanceKHR, xrGetVulkanGraphicsDevice2KHR and xrCreateVulkanDeviceKHR, which add the extensions
 *     the runtime needs and name the GPU the headset is on. (tools/win/xr-probe.c showed SteamVR accepts a Vulkan 1.3
 *     instance made this way.)
 *   - the session is made at the first stereo frame, on the core's device and queue, with a VIEW reference space (the
 *     picture is head-locked for now: the poses of the two eyes are half the eye separation to each side).
 *   - the swapchain is the stereo frame's size, B8G8R8A8_SRGB (else UNORM): the game's frame is raw display bytes, so
 *     it is copied raw (vkCmdCopyImage between the compatible formats) and the compositor decodes it as it is meant.
 *   - each frame the left half and the right half are the two eyes' sub-images of one projection layer, whose field of
 *     view is the game camera's view window (what the picture was drawn with), from the stereo value.
 *   - the queue is shared with the renderer's submits: everything that submits to it, or that the runtime may use it
 *     for (release, end frame), holds nvk_queue_lock().
 */
#include "oxr.h"

#if defined(XBOXRECOMP_HAVE_OPENXR) && defined(XBOXRECOMP_HAVE_VULKAN)

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <unknwn.h>
#define XR_USE_PLATFORM_WIN32
#else
#include <dlfcn.h>
#endif
#define XR_USE_GRAPHICS_API_VULKAN
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include "../d3d/nv2a_vk_core.h"
#include "../kernel/nv2a_pb_state.h"

#define VRLOG "[VR] "

#define XR_FUNCS(F) \
    F(xrCreateInstance) F(xrDestroyInstance) F(xrGetSystem) F(xrGetSystemProperties) F(xrPollEvent) \
    F(xrCreateSession) F(xrDestroySession) F(xrBeginSession) F(xrEndSession) F(xrCreateReferenceSpace) \
    F(xrDestroySpace) F(xrEnumerateSwapchainFormats) F(xrCreateSwapchain) F(xrDestroySwapchain) \
    F(xrEnumerateSwapchainImages) F(xrAcquireSwapchainImage) F(xrWaitSwapchainImage) F(xrReleaseSwapchainImage) \
    F(xrWaitFrame) F(xrBeginFrame) F(xrEndFrame) F(xrResultToString) F(xrGetVulkanGraphicsRequirements2KHR) \
    F(xrCreateVulkanInstanceKHR) F(xrGetVulkanGraphicsDevice2KHR) F(xrCreateVulkanDeviceKHR)
#define XR_DECL(n) static PFN_##n p_##n;
XR_FUNCS(XR_DECL)
static PFN_xrGetInstanceProcAddr p_xrGetInstanceProcAddr;

static struct {
    int inited, ok, failed, running, session_made, ended;
    void *lib;
    XrInstance inst;
    XrSystemId sys;
    XrSession sess;
    XrSpace space;
    XrSwapchain sc;
    uint32_t nimg, sw, sh;
    VkFormat sfmt;
    XrSwapchainImageVulkan2KHR *imgs;
    VkCommandPool pool;
    VkCommandBuffer *cbs;
    VkFence *fences;
    char sysname[XR_MAX_SYSTEM_NAME_SIZE];
    unsigned frames;
} X;

/* The first frames log each step before it runs (stderr is unbuffered): a crash inside the runtime then names its call. */
static unsigned s_step_n;
#define STEP(what) do { if (s_step_n < 40) { s_step_n++; fprintf(stderr, VRLOG "step: %s\n", what); } } while (0)

static const char *rs(XrResult r)
{
    static char b[XR_MAX_RESULT_STRING_SIZE];

    if (X.inst && p_xrResultToString && XR_SUCCEEDED(p_xrResultToString(X.inst, r, b)))
        return b;
    snprintf(b, sizeof b, "XrResult %d", (int)r);
    return b;
}

static void *open_loader(void)
{
#if defined(_WIN32)
    static const char *names[] = { "openxr_loader.dll", NULL, NULL };
    char steamvr[MAX_PATH];
    const char *pf = getenv("ProgramFiles(x86)");
    int i;
    void *h;

    if (pf) {
        snprintf(steamvr, sizeof steamvr, "%s\\Steam\\steamapps\\common\\SteamVR\\bin\\win64\\openxr_loader.dll", pf);
        names[1] = steamvr;
    }
    for (i = 0; i < 3 && !(h = names[i] ? (void *)LoadLibraryA(names[i]) : NULL); i++)
        ;
    return i < 3 ? h : NULL;
#else
    void *h = dlopen("libopenxr_loader.so.1", RTLD_NOW);

    return h ? h : dlopen("libopenxr_loader.so", RTLD_NOW);
#endif
}

static void *sym(const char *name)
{
#if defined(_WIN32)
    return (void *)GetProcAddress((HMODULE)X.lib, name);
#else
    return dlsym(X.lib, name);
#endif
}

/* ---- Vulkan creation, through the runtime ------------------------------------------------------- */

/* The function lookup the runtime is given. It calls vkCreateInstance and vkCreateDevice itself, with the create info
 * it has added to; those two must be the toolkit's own (vk_dyn.h: on Windows and Linux the Vulkan loader is opened at
 * run time and the instance's and the device's function pointers are loaded by those two wrappers: with the loader's
 * bare ones the next Vulkan call finds a null pointer). Everything else is the loader's. */
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL oxr_gipa(VkInstance instance, const char *name)
{
    if (!strcmp(name, "vkCreateInstance"))
        return (PFN_vkVoidFunction)vkCreateInstance;
    if (instance != VK_NULL_HANDLE && !strcmp(name, "vkCreateDevice"))
        return (PFN_vkVoidFunction)vkCreateDevice;
    return vkGetInstanceProcAddr(instance, name);
}

static VkResult hk_create_instance(const VkInstanceCreateInfo *ci, VkInstance *out)
{
    XrVulkanInstanceCreateInfoKHR xi;
    VkResult vr = VK_SUCCESS;
    XrResult r;

    memset(&xi, 0, sizeof xi);
    xi.type = XR_TYPE_VULKAN_INSTANCE_CREATE_INFO_KHR;
    xi.systemId = X.sys;
    xi.pfnGetInstanceProcAddr = oxr_gipa;
    xi.vulkanCreateInfo = ci;
    r = p_xrCreateVulkanInstanceKHR(X.inst, &xi, out, &vr);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateVulkanInstanceKHR: %s\n", rs(r));
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (vr == VK_SUCCESS)
        fprintf(stderr, VRLOG "the Vulkan instance was made through the OpenXR runtime\n");
    return vr;
}

static VkPhysicalDevice hk_physical_device(VkInstance inst)
{
    XrVulkanGraphicsDeviceGetInfoKHR gi;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    XrResult r;

    memset(&gi, 0, sizeof gi);
    gi.type = XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR;
    gi.systemId = X.sys;
    gi.vulkanInstance = inst;
    r = p_xrGetVulkanGraphicsDevice2KHR(X.inst, &gi, &pd);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrGetVulkanGraphicsDevice2KHR: %s\n", rs(r));
        return VK_NULL_HANDLE;
    }
    return pd;
}

static VkResult hk_create_device(VkPhysicalDevice pd, const VkDeviceCreateInfo *ci, VkDevice *out)
{
    XrVulkanDeviceCreateInfoKHR di;
    VkResult vr = VK_SUCCESS;
    XrResult r;

    memset(&di, 0, sizeof di);
    di.type = XR_TYPE_VULKAN_DEVICE_CREATE_INFO_KHR;
    di.systemId = X.sys;
    di.pfnGetInstanceProcAddr = oxr_gipa;
    di.vulkanPhysicalDevice = pd;
    di.vulkanCreateInfo = ci;
    r = p_xrCreateVulkanDeviceKHR(X.inst, &di, out, &vr);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateVulkanDeviceKHR: %s\n", rs(r));
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    if (vr == VK_SUCCESS)
        fprintf(stderr, VRLOG "the Vulkan device was made through the OpenXR runtime\n");
    return vr;
}

static const struct nvk_create_hooks s_hooks = { hk_create_instance, hk_physical_device, hk_create_device };

int xbox_VrInit(void)
{
    XrInstanceCreateInfo ci;
    XrSystemGetInfo gi;
    XrSystemProperties sp;
    XrGraphicsRequirementsVulkan2KHR req;
    static const char *exts[] = { "XR_KHR_vulkan_enable2" };
    XrResult r;

    if (X.inited)
        return X.ok;
    X.inited = 1;
    X.lib = open_loader();
    if (!X.lib) {
        fprintf(stderr, VRLOG "no OpenXR loader (openxr_loader.dll): VR off\n");
        return 0;
    }
    p_xrGetInstanceProcAddr = (PFN_xrGetInstanceProcAddr)sym("xrGetInstanceProcAddr");
    if (!p_xrGetInstanceProcAddr) {
        fprintf(stderr, VRLOG "the OpenXR loader has no xrGetInstanceProcAddr: VR off\n");
        return 0;
    }
    p_xrGetInstanceProcAddr(XR_NULL_HANDLE, "xrCreateInstance", (PFN_xrVoidFunction *)&p_xrCreateInstance);
    if (!p_xrCreateInstance)
        return 0;
    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_INSTANCE_CREATE_INFO;
    ci.applicationInfo.apiVersion = XR_API_VERSION_1_0;
    snprintf(ci.applicationInfo.applicationName, sizeof ci.applicationInfo.applicationName, "Takedawn");
    snprintf(ci.applicationInfo.engineName, sizeof ci.applicationInfo.engineName, "xboxrecomp");
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = exts;
    r = p_xrCreateInstance(&ci, &X.inst);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateInstance: %d (no OpenXR runtime, or it lacks XR_KHR_vulkan_enable2): VR off\n", (int)r);
        return 0;
    }
#define XR_LOAD(n) \
    if (XR_FAILED(p_xrGetInstanceProcAddr(X.inst, #n, (PFN_xrVoidFunction *)&p_##n)) || !p_##n) { \
        fprintf(stderr, VRLOG "the runtime has no %s: VR off\n", #n); \
        return 0; \
    }
    XR_FUNCS(XR_LOAD)
#undef XR_LOAD
    memset(&gi, 0, sizeof gi);
    gi.type = XR_TYPE_SYSTEM_GET_INFO;
    gi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    r = p_xrGetSystem(X.inst, &gi, &X.sys);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "no headset: %s (connect it and start the runtime: SteamVR): VR off\n", rs(r));
        return 0;
    }
    memset(&sp, 0, sizeof sp);
    sp.type = XR_TYPE_SYSTEM_PROPERTIES;
    p_xrGetSystemProperties(X.inst, X.sys, &sp);
    snprintf(X.sysname, sizeof X.sysname, "%s", sp.systemName);
    memset(&req, 0, sizeof req);
    req.type = XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN2_KHR;
    r = p_xrGetVulkanGraphicsRequirements2KHR(X.inst, X.sys, &req);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrGetVulkanGraphicsRequirements2KHR: %s: VR off\n", rs(r));
        return 0;
    }
    nvk_set_create_hooks(&s_hooks);
    X.ok = 1;
    fprintf(stderr, VRLOG "headset \"%s\": the Vulkan instance and device will be made through the OpenXR runtime\n",
            X.sysname);
    return 1;
}

int xbox_VrActive(void)
{
    return X.ok && !X.failed && !X.ended;
}

/* ---- the session ---------------------------------------------------------------------------------- */

static void fail(const char *why)
{
    if (!X.failed)
        fprintf(stderr, VRLOG "%s: the headset output stops (the window goes on)\n", why);
    X.failed = 1;
}

static int make_session(const struct nvk_core *C)
{
    XrGraphicsBindingVulkan2KHR gb;
    XrSessionCreateInfo si;
    XrReferenceSpaceCreateInfo rsi;
    XrResult r;

    memset(&gb, 0, sizeof gb);
    gb.type = XR_TYPE_GRAPHICS_BINDING_VULKAN2_KHR;
    gb.instance = C->inst;
    gb.physicalDevice = C->phys;
    gb.device = C->dev;
    gb.queueFamilyIndex = C->qfam;
    gb.queueIndex = 0;
    memset(&si, 0, sizeof si);
    si.type = XR_TYPE_SESSION_CREATE_INFO;
    si.next = &gb;
    si.systemId = X.sys;
    STEP("xrCreateSession");
    nvk_queue_lock();
    r = p_xrCreateSession(X.inst, &si, &X.sess);
    nvk_queue_unlock();
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateSession: %s\n", rs(r));
        return 0;
    }
    memset(&rsi, 0, sizeof rsi);
    rsi.type = XR_TYPE_REFERENCE_SPACE_CREATE_INFO;
    rsi.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;     /* head-locked, for now */
    rsi.poseInReferenceSpace.orientation.w = 1.0f;
    STEP("xrCreateReferenceSpace");
    r = p_xrCreateReferenceSpace(X.sess, &rsi, &X.space);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateReferenceSpace: %s\n", rs(r));
        return 0;
    }
    X.session_made = 1;
    fprintf(stderr, VRLOG "session made on the renderer's device and queue (family %u)\n", C->qfam);
    return 1;
}

static void poll_events(void)
{
    XrEventDataBuffer ev;

    for (;;) {
        XrResult r;

        memset(&ev, 0, sizeof ev);
        ev.type = XR_TYPE_EVENT_DATA_BUFFER;
        r = p_xrPollEvent(X.inst, &ev);
        if (r != XR_SUCCESS)
            break;
        if (ev.type == XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED) {
            const XrEventDataSessionStateChanged *e = (const XrEventDataSessionStateChanged *)&ev;

            if (e->state == XR_SESSION_STATE_READY && !X.running) {
                XrSessionBeginInfo bi;

                memset(&bi, 0, sizeof bi);
                bi.type = XR_TYPE_SESSION_BEGIN_INFO;
                bi.primaryViewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                STEP("xrBeginSession");
                nvk_queue_lock();
                r = p_xrBeginSession(X.sess, &bi);
                nvk_queue_unlock();
                if (XR_FAILED(r))
                    fprintf(stderr, VRLOG "xrBeginSession: %s\n", rs(r));
                else {
                    X.running = 1;
                    fprintf(stderr, VRLOG "the session is running\n");
                }
            } else if (e->state == XR_SESSION_STATE_STOPPING && X.running) {
                p_xrEndSession(X.sess);
                X.running = 0;
                fprintf(stderr, VRLOG "the session stopped\n");
            } else if (e->state == XR_SESSION_STATE_EXITING || e->state == XR_SESSION_STATE_LOSS_PENDING) {
                X.running = 0;
                X.ended = 1;
                fprintf(stderr, VRLOG "the session is ending (the headset or SteamVR was closed)\n");
            }
        } else if (ev.type == XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING) {
            X.running = 0;
            X.ended = 1;
            fprintf(stderr, VRLOG "the runtime is going away\n");
        }
    }
}

static int make_swapchain(const struct nvk_core *C, uint32_t w, uint32_t h)
{
    uint32_t n = 0, i;
    int64_t *fmts, pick = 0;
    XrSwapchainCreateInfo ci;
    VkCommandPoolCreateInfo pi;
    VkCommandBufferAllocateInfo ai;
    VkFenceCreateInfo fi;
    XrResult r;

    STEP("xrEnumerateSwapchainFormats");
    p_xrEnumerateSwapchainFormats(X.sess, 0, &n, NULL);
    fmts = (int64_t *)calloc(n ? n : 1, sizeof *fmts);
    if (!fmts)
        return 0;
    p_xrEnumerateSwapchainFormats(X.sess, n, &n, fmts);
    for (i = 0; i < n; i++)
        if (fmts[i] == (int64_t)VK_FORMAT_B8G8R8A8_SRGB)
            pick = fmts[i];
    for (i = 0; i < n && !pick; i++)
        if (fmts[i] == (int64_t)VK_FORMAT_B8G8R8A8_UNORM)
            pick = fmts[i];
    if (!pick) {
        fprintf(stderr, VRLOG "the runtime offers no B8G8R8A8 swapchain format (%u formats): the frame cannot be copied raw\n", n);
        free(fmts);
        return 0;
    }
    free(fmts);
    memset(&ci, 0, sizeof ci);
    ci.type = XR_TYPE_SWAPCHAIN_CREATE_INFO;
    ci.usageFlags = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT | XR_SWAPCHAIN_USAGE_TRANSFER_DST_BIT
                  | XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
    ci.format = pick;
    ci.sampleCount = 1;
    ci.width = w;
    ci.height = h;
    ci.faceCount = 1;
    ci.arraySize = 1;
    ci.mipCount = 1;
    STEP("xrCreateSwapchain");
    nvk_queue_lock();
    r = p_xrCreateSwapchain(X.sess, &ci, &X.sc);
    nvk_queue_unlock();
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrCreateSwapchain %ux%u: %s\n", w, h, rs(r));
        return 0;
    }
    STEP("xrEnumerateSwapchainImages (count)");
    p_xrEnumerateSwapchainImages(X.sc, 0, &X.nimg, NULL);
    X.imgs = (XrSwapchainImageVulkan2KHR *)calloc(X.nimg, sizeof *X.imgs);
    X.cbs = (VkCommandBuffer *)calloc(X.nimg, sizeof *X.cbs);
    X.fences = (VkFence *)calloc(X.nimg, sizeof *X.fences);
    if (!X.imgs || !X.cbs || !X.fences)
        return 0;
    for (i = 0; i < X.nimg; i++)
        X.imgs[i].type = XR_TYPE_SWAPCHAIN_IMAGE_VULKAN2_KHR;
    STEP("xrEnumerateSwapchainImages (images)");
    r = p_xrEnumerateSwapchainImages(X.sc, X.nimg, &X.nimg, (XrSwapchainImageBaseHeader *)X.imgs);
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrEnumerateSwapchainImages: %s\n", rs(r));
        return 0;
    }
    memset(&pi, 0, sizeof pi);
    pi.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pi.queueFamilyIndex = C->qfam;
    STEP("the command pool, buffers and fences");
    if (vkCreateCommandPool(C->dev, &pi, NULL, &X.pool) != VK_SUCCESS)
        return 0;
    memset(&ai, 0, sizeof ai);
    ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    ai.commandPool = X.pool;
    ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ai.commandBufferCount = X.nimg;
    if (vkAllocateCommandBuffers(C->dev, &ai, X.cbs) != VK_SUCCESS)
        return 0;
    memset(&fi, 0, sizeof fi);
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (i = 0; i < X.nimg; i++)
        if (vkCreateFence(C->dev, &fi, NULL, &X.fences[i]) != VK_SUCCESS)
            return 0;
    X.sw = w;
    X.sh = h;
    X.sfmt = (VkFormat)pick;
    fprintf(stderr, VRLOG "swapchain %ux%u, %u images, format %s\n", w, h, X.nimg,
            X.sfmt == VK_FORMAT_B8G8R8A8_SRGB ? "B8G8R8A8_SRGB" : "B8G8R8A8_UNORM");
    return 1;
}

/* The raw copy of the frame into swapchain image `idx`, submitted. */
static int copy_frame(const struct nvk_core *C, VkImage slot, uint32_t idx)
{
    VkCommandBuffer cb = X.cbs[idx];
    VkCommandBufferBeginInfo bi;
    VkImageCopy ic;
    VkSubmitInfo si;
    VkResult r;
    VkImage dst = X.imgs[idx].image;

    vkWaitForFences(C->dev, 1, &X.fences[idx], VK_TRUE, UINT64_MAX);
    vkResetFences(C->dev, 1, &X.fences[idx]);
    vkResetCommandBuffer(cb, 0);
    memset(&bi, 0, sizeof bi);
    bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    nvk_image_barrier(cb, slot, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL);
    nvk_image_barrier(cb, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    memset(&ic, 0, sizeof ic);
    ic.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    ic.srcSubresource.layerCount = 1;
    ic.dstSubresource = ic.srcSubresource;
    ic.extent.width = X.sw;
    ic.extent.height = X.sh;
    ic.extent.depth = 1;
    vkCmdCopyImage(cb, slot, VK_IMAGE_LAYOUT_GENERAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);
    nvk_image_barrier(cb, dst, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    vkEndCommandBuffer(cb);
    memset(&si, 0, sizeof si);
    si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    nvk_queue_lock();
    r = vkQueueSubmit(C->queue, 1, &si, X.fences[idx]);
    nvk_queue_unlock();
    return r == VK_SUCCESS;
}

void xbox_VrPresent(const struct nvk_core *C, VkImage slot, uint32_t w, uint32_t h, VkFormat fmt, uint32_t stereo,
                    uint32_t flip)
{
    XrFrameWaitInfo wi;
    XrFrameState fs;
    XrFrameBeginInfo bgi;
    XrFrameEndInfo ei;
    XrCompositionLayerProjection lp;
    XrCompositionLayerProjectionView pv[2];
    const XrCompositionLayerBaseHeader *layers[1];
    XrResult r;
    uint32_t idx = 0;
    int have_layer = 0;
    float k, vwx, vwy, ipd;

    (void)fmt;
    (void)flip;
    if (!xbox_VrActive() || !C || !(stereo & NV2A_STEREO_ON) || !w || !h)
        return;
    if (!X.session_made && !make_session(C)) {
        fail("no session");
        return;
    }
    poll_events();
    if (!X.running)
        return;                              /* READY has not come yet, or the session paused: the window goes on */
    if (!X.sc && !make_swapchain(C, w, h)) {
        fail("no swapchain");
        return;
    }
    if (w != X.sw || h != X.sh)
        return;                              /* the picture's size changed: one swapchain size per run */
    memset(&wi, 0, sizeof wi);
    wi.type = XR_TYPE_FRAME_WAIT_INFO;
    memset(&fs, 0, sizeof fs);
    fs.type = XR_TYPE_FRAME_STATE;
    STEP("xrWaitFrame");
    r = p_xrWaitFrame(X.sess, &wi, &fs);
    if (XR_FAILED(r)) {
        fail("xrWaitFrame");
        return;
    }
    memset(&bgi, 0, sizeof bgi);
    bgi.type = XR_TYPE_FRAME_BEGIN_INFO;
    STEP("xrBeginFrame");
    nvk_queue_lock();
    r = p_xrBeginFrame(X.sess, &bgi);
    nvk_queue_unlock();
    if (XR_FAILED(r)) {
        fprintf(stderr, VRLOG "xrBeginFrame: %s\n", rs(r));
        return;
    }
    k = nv2a_stereo_k(stereo);
    vwx = nv2a_stereo_vwx(stereo);
    vwy = nv2a_stereo_vwy(stereo);
    ipd = 2.0f * k * vwx;                    /* K = ipd / 2 / vw.x */
    if (fs.shouldRender && vwx > 0.0f && vwy > 0.0f) {
        XrSwapchainImageAcquireInfo ai;
        XrSwapchainImageWaitInfo wsi;

        memset(&ai, 0, sizeof ai);
        ai.type = XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO;
        STEP("xrAcquireSwapchainImage");
        nvk_queue_lock();
        r = p_xrAcquireSwapchainImage(X.sc, &ai, &idx);
        nvk_queue_unlock();
        if (XR_SUCCEEDED(r)) {
            memset(&wsi, 0, sizeof wsi);
            wsi.type = XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO;
            wsi.timeout = XR_INFINITE_DURATION;
            STEP("xrWaitSwapchainImage");
            r = p_xrWaitSwapchainImage(X.sc, &wsi);
        }
        STEP("the copy");
        if (XR_SUCCEEDED(r) && copy_frame(C, slot, idx)) {
            XrSwapchainImageReleaseInfo ri;
            int e;

            memset(&ri, 0, sizeof ri);
            ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            STEP("xrReleaseSwapchainImage");
            nvk_queue_lock();
            p_xrReleaseSwapchainImage(X.sc, &ri);
            nvk_queue_unlock();
            memset(&lp, 0, sizeof lp);
            lp.type = XR_TYPE_COMPOSITION_LAYER_PROJECTION;
            lp.space = X.space;
            lp.viewCount = 2;
            lp.views = pv;
            for (e = 0; e < 2; e++) {
                memset(&pv[e], 0, sizeof pv[e]);
                pv[e].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
                pv[e].pose.orientation.w = 1.0f;
                pv[e].pose.position.x = (e ? 0.5f : -0.5f) * ipd;
                pv[e].fov.angleLeft = -atanf(vwx);
                pv[e].fov.angleRight = atanf(vwx);
                pv[e].fov.angleUp = atanf(vwy);
                pv[e].fov.angleDown = -atanf(vwy);
                pv[e].subImage.swapchain = X.sc;
                pv[e].subImage.imageRect.offset.x = (int32_t)(e * (w / 2u));
                pv[e].subImage.imageRect.extent.width = (int32_t)(w / 2u);
                pv[e].subImage.imageRect.extent.height = (int32_t)h;
            }
            layers[0] = (const XrCompositionLayerBaseHeader *)&lp;
            have_layer = 1;
        } else if (XR_SUCCEEDED(r)) {
            XrSwapchainImageReleaseInfo ri;

            memset(&ri, 0, sizeof ri);
            ri.type = XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO;
            p_xrReleaseSwapchainImage(X.sc, &ri);
        }
    }
    memset(&ei, 0, sizeof ei);
    ei.type = XR_TYPE_FRAME_END_INFO;
    ei.displayTime = fs.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount = have_layer ? 1u : 0u;
    ei.layers = have_layer ? layers : NULL;
    STEP("xrEndFrame");
    nvk_queue_lock();
    r = p_xrEndFrame(X.sess, &ei);
    nvk_queue_unlock();
    if (XR_FAILED(r))
        fprintf(stderr, VRLOG "xrEndFrame: %s\n", rs(r));
    if (X.frames++ == 0)
        fprintf(stderr, VRLOG "first stereo frame to the headset: eye separation %.3f m, view window %.3f x %.3f "
                "(field of view %.0f x %.0f degrees)\n", ipd, vwx, vwy, 2.0 * atan((double)vwx) * 57.29578,
                2.0 * atan((double)vwy) * 57.29578);
}

void xbox_VrShutdown(void)
{
    if (!X.inited || !X.inst)
        return;
    if (X.running && X.sess)
        p_xrEndSession(X.sess);
    if (X.sc)
        p_xrDestroySwapchain(X.sc);
    if (X.space)
        p_xrDestroySpace(X.space);
    if (X.sess)
        p_xrDestroySession(X.sess);
    p_xrDestroyInstance(X.inst);
    memset(&X, 0, sizeof X);
}

#else /* no OpenXR headers or no Vulkan: stubs */

#include <stdio.h>

int xbox_VrInit(void)
{
    fprintf(stderr, "[VR] this build has no OpenXR support (XBOXRECOMP_OPENXR_INCLUDE_DIR): VR off\n");
    return 0;
}
int xbox_VrActive(void) { return 0; }
void xbox_VrPresent(const struct nvk_core *C, VkImage slot, uint32_t w, uint32_t h, VkFormat fmt, uint32_t stereo,
                    uint32_t flip)
{
    (void)C; (void)slot; (void)w; (void)h; (void)fmt; (void)stereo; (void)flip;
}
void xbox_VrShutdown(void) {}

#endif
