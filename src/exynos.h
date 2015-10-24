/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifndef EXYNOS_H
#define EXYNOS_H

#ifdef TEMP_D
#define XV 1
#define LAYER_MANAGER 1
#endif

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include "xorg-server.h"
#include "xf86.h"
#include "xf86xv.h"
#include "xf86_OSproc.h"
#include "xf86drm.h"
#include "X11/Xatom.h"
#include "exynos.h"
#include "tbm_bufmgr.h"
#include "exynos_display.h"
#include "exynos_accel.h"
#include "exynos_video.h"
#include "exynos_wb.h"
#include "exynos_util.h"
#include "exynos_hwc.h"
#include "exynos_hwa.h"
#if HAVE_UDEV
#include <libudev.h>
#endif

#ifdef ENABLE_TTRACE
#include <ttrace.h>
#ifdef TTRACE_VIDEO_BEGIN
#undef TTRACE_VIDEO_BEGIN
#endif
#ifdef TTRACE_VIDEO_END
#undef TTRACE_VIDEO_END
#endif
#ifdef TTRACE_GRAPHICS_BEGIN
#undef TTRACE_GRAPHICS_BEGIN
#endif
#ifdef TTRACE_GRAPHICS_END
#undef TTRACE_GRAPHICS_END
#endif
#define TTRACE_VIDEO_BEGIN(NAME) traceBegin(TTRACE_TAG_VIDEO, NAME)
#define TTRACE_VIDEO_END() traceEnd(TTRACE_TAG_VIDEO)

#define TTRACE_GRAPHICS_BEGIN(NAME) traceBegin(TTRACE_TAG_GRAPHICS, NAME)
#define TTRACE_GRAPHICS_END() traceEnd(TTRACE_TAG_GRAPHICS)
#else
#ifdef TTRACE_VIDEO_BEGIN
#undef TTRACE_VIDEO_BEGIN
#endif
#ifdef TTRACE_VIDEO_END
#undef TTRACE_VIDEO_END
#endif
#ifdef TTRACE_GRAPHICS_BEGIN(
#undef TTRACE_GRAPHICS_BEGIN
#endif
#ifdef TTRACE_GRAPHICS_END
#undef TTRACE_GRAPHICS_END
#endif
#define TTRACE_VIDEO_BEGIN(NAME)
#define TTRACE_VIDEO_END()

#define TTRACE_GRAPHICS_BEGIN(NAME)
#define TTRACE_GRAPHICS_END()
#endif

#define USE_XDBG 0

#define DUMP_TYPE_PNG "png"
#define DUMP_TYPE_BMP "bmp"
#define DUMP_TYPE_RAW "raw"

/* drm bo data type */
typedef enum {
    TBM_BO_DATA_FB = 1,
} TBM_BO_DATA;

/* framebuffer infomation */
typedef struct {
    ScrnInfoPtr pScrn;

    int width;
    int height;

    int num_bo;
    struct xorg_list list_bo;
    void *tbl_bo;               /* bo hash table: key=(x,y)position */

    tbm_bo default_bo;
} EXYNOSFbRec, *EXYNOSFbPtr;

/* framebuffer bo data */
typedef struct {
    EXYNOSFbPtr pFb;
    BoxRec pos;
    uint32_t gem_handle;
    intptr_t fb_id;
    int pitch;
    int size;
    PixmapPtr pPixmap;
    ScrnInfoPtr pScrn;
} EXYNOSFbBoDataRec, *EXYNOSFbBoDataPtr;

/* exynos screen private information */
typedef struct {
    EntityInfoPtr pEnt;
    Bool fake_root;             /* screen rotation status */

    /* driver options */
    OptionInfoPtr Options;
    Bool is_exa;
    Bool is_dri2;
    Bool is_present;
    Bool is_dri3;
    Bool is_sw_exa;
    Bool is_accel_2d;
    Bool is_wb_clone;
    Bool is_tfb;                /* triple flip buffer */
    Bool cachable;              /* if use cachable buffer */
    Bool scanout;               /* if use scanout buffer */
    int flip_bufs;              /* number of the flip buffers */
    Rotation rotate;
    Bool use_partial_update;
    Bool is_fb_touched;         /* whether framebuffer is touched or not */

    Bool use_hwc;
    Bool use_setplane;
    Bool use_flip;
    Bool use_hwa;

    /* drm */
    int drm_fd;
    char *drm_device_name;
    tbm_bufmgr tbm_bufmgr;

    /* main fb information */
    EXYNOSFbPtr pFb;

    /* mode setting info private */
    EXYNOSModePtr pExynosMode;

    /* exa private */
    EXYNOSExaPrivPtr pExaPriv;

    /* video private */
    EXYNOSVideoPrivPtr pVideoPriv;

    /* HWC Compositor */
    Bool hwc_active;
    Bool hwc_use_def_layer;
    EXYNOSHwcPtr pHwc;
#ifdef LAYER_MANAGER
    /* Layer Manager */
    void *pLYRM;
#endif
    Bool isLcdOff;              /* lvds connector on/off status */
#ifdef NO_CRTC_MODE
    Bool isCrtcOn;              /* Global crtc status */
#endif
    /* screen wrapper functions */
    CloseScreenProcPtr CloseScreen;
    CreateScreenResourcesProcPtr CreateScreenResources;

#if HAVE_UDEV
    struct udev_monitor *uevent_monitor;
    InputHandlerProc uevent_handler;
#endif

    /* DRI2 */
    Atom atom_use_dri2;
    Bool useAsyncSwap;
    DrawablePtr flipDrawable;

    /* pending flip handler cause of lcd off */
    Bool pending_flip_handler;
    unsigned int frame;
    unsigned int tv_sec;
    unsigned int tv_usec;
    void *event_data;

    /* overlay drawable */
    DamagePtr ovl_damage;
    DrawablePtr ovl_drawable;

    EXYNOSDisplaySetMode set_mode;
    OsTimerPtr resume_timer;

    EXYNOSWb *wb_clone;
    Bool wb_fps;
    int wb_hz;

    /* Cursor */
    Bool enableCursor;

    /* dump */
    int dump_mode;
    long dump_xid;
    void *dump_info;
    char *dump_str;
    char dump_type[16];
    int xvperf_mode;
    char *xvperf;

    int scale;
    int cpu;
    int flip_cnt;

    /* mem debug
       Normal pixmap
       CREATE_PIXMAP_USAGE_BACKING_PIXMAP
       CREATE_PIXMAP_USAGE_OVERLAY
       CREATE_PIXMAP_USAGE_XVIDEO
       CREATE_PIXMAP_USAGE_DRI2_FILP_BACK
       CREATE_PIXMAP_USAGE_FB
       CREATE_PIXMAP_USAGE_SUB_FB
       CREATE_PIXMAP_USAGE_DRI2_BACK
       CREATE_PIXMAP_USAGE_DRI3_BACK
     */
    int pix_normal;
    int pix_backing_pixmap;
    int pix_overlay;
    int pix_dri2_flip_back;
    int pix_fb;
    int pix_sub_fb;
    int pix_dri2_back;
    int pix_dri3_back;

    /* HWA */
    int XVHIDE;

} EXYNOSRec, *EXYNOSPtr;

/* get a private screen of ScrnInfo */
#define EXYNOSPTR(p) ((EXYNOSPtr)((p)->driverPrivate))

/* the version of the driver */
#define EXYNOS_VERSION 1000

/* the name used to prefix messages */
#define EXYNOS_NAME "exynos"

/* the driver name used in display.conf file. exynos_drv.so */
#define EXYNOS_DRIVER_NAME "exynos"

#define ROOT_FB_ADDR (~0UL)

#define EXYNOS_CURSOR_W 128
#define EXYNOS_CURSOR_H 128

/* exynos framebuffer */
EXYNOSFbPtr exynosFbAllocate(ScrnInfoPtr pScrn, int width, int height);
void exynosFbFree(EXYNOSFbPtr pFb);
void exynosFbResize(EXYNOSFbPtr pFb, int width, int height);
tbm_bo exynosFbGetBo(EXYNOSFbPtr pFb, int x, int y, int width, int height,
                     Bool onlyIfExists);
int exynosFbFindBo(EXYNOSFbPtr pFb, int x, int y, int width, int height,
                   int *num_bo, tbm_bo ** bos);
tbm_bo exynosFbFindBoByPoint(EXYNOSFbPtr pFb, int x, int y);
tbm_bo exynosFbSwapBo(EXYNOSFbPtr pFb, tbm_bo back_bo);

tbm_bo exynosRenderBoCreate(ScrnInfoPtr pScrn, int width, int height);
int exynosSwapToRenderBo(ScrnInfoPtr pScrn, int width, int height,
                         tbm_bo carr_bo, Bool clear);
tbm_bo exynosRenderBoRef(tbm_bo bo);
void exynosRenderBoUnref(tbm_bo bo);
void exynosRenderBoSetPos(tbm_bo bo, int x, int y);
PixmapPtr exynosRenderBoGetPixmap(EXYNOSFbPtr pFb, tbm_bo bo);

#endif                          /* EXYNOS_H */
