/*
 * oxr.h - a VR headset through OpenXR (src/video/oxr.c).
 *
 * The renderer draws a stereo picture as one frame with the left eye in its left half and the right eye in its
 * right half (nv2a_pb_state.h's stereo value). This module puts that frame in a headset: the Vulkan instance and
 * device are made through the OpenXR runtime (XR_KHR_vulkan_enable2: the runtime adds what it needs and names the
 * GPU), each stereo frame the presenter shows is copied raw into an OpenXR swapchain image, and submitted as a
 * stereo projection layer, the halves as the two eyes' sub-images. The window keeps showing the side-by-side
 * picture. The OpenXR loader is opened at run time (openxr_loader.dll, libopenxr_loader.so.1), so a PC without
 * one starts and says so.
 *
 * Built only with the OpenXR headers (XBOXRECOMP_OPENXR_INCLUDE_DIR) and Vulkan; otherwise these are stubs.
 */
#ifndef XBOX_OXR_H
#define XBOX_OXR_H

#include <stdint.h>
#include <vulkan/vulkan.h>

struct nvk_core;

/* Before the window and the Vulkan device exist (the host's start-up): the loader, an OpenXR instance and the
 * headset's system, and the Vulkan creation hooks installed. 1 when a headset system is there; 0 (with the reason on
 * stderr) and nothing changed otherwise. */
int  xbox_VrInit(void);

/* 1 after a successful xbox_VrInit and until the session fails or ends. */
int  xbox_VrActive(void);

/* A side-by-side stereo frame to the headset (the presenter's thread, with the queue free): `slot` is the frame's
 * image (GENERAL layout, B8G8R8A8_UNORM, w x h, the left eye in the left half), `stereo` the frame's stereo value
 * (nv2a_pb_state.h: the eye shift and the view window the layer's field of view comes from), `flip` for the log.
 * Does the OpenXR frame loop (events, wait, begin, copy, end) once; returns at once when the session is not running. */
void xbox_VrPresent(const struct nvk_core *C, VkImage slot, uint32_t w, uint32_t h, VkFormat fmt, uint32_t stereo,
                    uint32_t flip);

/* The end of the run. */
void xbox_VrShutdown(void);

#endif /* XBOX_OXR_H */
