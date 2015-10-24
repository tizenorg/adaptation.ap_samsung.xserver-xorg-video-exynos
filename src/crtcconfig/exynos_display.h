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

#ifndef __SEC_DISPLAY_H__
#define __SEC_DISPLAY_H__

#include <xf86drmMode.h>
#include <xf86Crtc.h>
#include <tbm_bufmgr.h>
#include <list.h>

#define DBG_DRM_EVENT 1

typedef enum {
    DISPLAY_SET_MODE_OFF,
    DISPLAY_SET_MODE_CLONE,
    DISPLAY_SET_MODE_EXT,
    DISPLAY_SET_MODE_MIRROR,
} EXYNOSDisplaySetMode;

typedef enum {
    DISPLAY_CONN_MODE_NONE,
    DISPLAY_CONN_MODE_LVDS,
    DISPLAY_CONN_MODE_HDMI,
    DISPLAY_CONN_MODE_VIRTUAL,
    DISPLAY_CONN_MODE_DUMMY,
    DISPLAY_CONN_MODE_MAX,
} EXYNOSDisplayConnMode;

typedef enum {
    VBLNAK_INFO_NONE,
    VBLANK_INFO_SWAP,
    VBLANK_INFO_PLANE,
    VBLANK_INFO_PRESENT,
    VBLANK_INFO_PAGE_FLIP,
    VBLANK_INFO_MAX
} EXYNOSVBlankInfoType;

typedef struct _exynosDrmEventContext {
    void (*vblank_handler) (int fd,
                            unsigned int sequence,
                            unsigned int tv_sec,
                            unsigned int tv_usec, void *user_data);

    void (*page_flip_handler) (int fd,
                               unsigned int sequence,
                               unsigned int tv_sec,
                               unsigned int tv_usec, void *user_data);

    void (*g2d_handler) (int fd,
                         unsigned int cmdlist_no,
                         unsigned int tv_sec,
                         unsigned int tv_usec, void *user_data);

    void (*ipp_handler) (int fd,
                         unsigned int prop_id,
                         unsigned int *buf_idx,
                         unsigned int tv_sec,
                         unsigned int tv_usec, void *user_data);
} exynosDrmEventContext, *exynosDrmEventContextPtr;

typedef struct _exynosDrmMode {
    int type;
    int fd;
    drmModeResPtr mode_res;
    drmModePlaneResPtr plane_res;
    int cpp;
    drmModeModeInfo main_lcd_mode;
    drmModeModeInfo ext_connector_mode;

    exynosDrmEventContext event_context;

    struct xorg_list outputs;
    struct xorg_list crtcs;
    struct xorg_list planes;
#ifdef NO_CRTC_MODE
    int num_dummy_output;
    int num_real_output;
    int num_real_crtc;
    int num_dummy_crtc;
#endif
    EXYNOSDisplaySetMode set_mode;
    EXYNOSDisplayConnMode conn_mode;
    int rotate;

    int unset_connector_type;
} EXYNOSModeRec, *EXYNOSModePtr;

typedef void (*EXYNOSFlipEventHandler) (unsigned int frame,
                                        unsigned int tv_exynos,
                                        unsigned int tv_uexynos,
                                        void *event_data, Bool flip_failed);

typedef struct _exynosPageFlip {
    xf86CrtcPtr pCrtc;
    Bool dispatch_me;
    Bool clone;
    Bool flip_failed;

    tbm_bo back_bo;
    tbm_bo accessibility_back_bo;
    int fb_id;

    void *data;
    CARD32 time;

    EXYNOSFlipEventHandler handler;

#if DBG_DRM_EVENT
    void *xdbg_log_pageflip;
#endif
} EXYNOSPageFlipRec, *EXYNOSPageFlipPtr;

typedef struct _exynosVBlankInfo {
    EXYNOSVBlankInfoType type;
    void *data;                 /* request data pointer */
    CARD32 time;

#if DBG_DRM_EVENT
    void *xdbg_log_vblank;
#endif
} EXYNOSVBlankInfoRec, *EXYNOSVBlankInfoPtr;

typedef struct _exynosProperty {
    drmModePropertyPtr mode_prop;
    uint64_t value;
    int num_atoms;              /* if range prop, num_atoms == 1; if enum prop, num_atoms == num_enums + 1 */
    Atom *atoms;
} EXYNOSPropertyRec, *EXYNOSPropertyPtr;

Bool exynosModePreInit(ScrnInfoPtr pScrn, int drm_fd);
void exynosModeInit(ScrnInfoPtr pScrn);
void exynosModeDeinit(ScrnInfoPtr pScrn);
xf86CrtcPtr exynosModeCoveringCrtc(ScrnInfoPtr pScrn, BoxPtr pBox,
                                   xf86CrtcPtr pDesiredCrtc, BoxPtr pBoxCrtc);
int exynosModeGetCrtcPipe(xf86CrtcPtr pCrtc);
Bool exynosModePageFlip(ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void *flip_info,
                        int pipe, tbm_bo back_bo, RegionPtr pFlipRegion,
                        unsigned int client_idx, XID drawable_id,
                        EXYNOSFlipEventHandler handler, Bool change_front);
void exynosModeLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
                           LOCO * colors, VisualPtr pVisual);

void exynosDisplaySwapModeFromKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode,
                                    DisplayModePtr pMode);
void exynosDisplayModeFromKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode,
                                DisplayModePtr pMode);
void exynosDisplaySwapModeToKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode,
                                  DisplayModePtr pMode);
void exynosDisplayModeToKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode,
                              DisplayModePtr pMode);

Bool exynosDisplaySetDispSetMode(ScrnInfoPtr pScrn,
                                 EXYNOSDisplaySetMode disp_mode);
EXYNOSDisplaySetMode exynosDisplayGetDispSetMode(ScrnInfoPtr pScrn);
Bool exynosDisplaySetDispRotate(ScrnInfoPtr pScrn, int disp_rotate);
int exynosDisplayGetDispRotate(ScrnInfoPtr pScrn);
Bool exynosDisplaySetDispConnMode(ScrnInfoPtr pScrn,
                                  EXYNOSDisplayConnMode disp_conn);
EXYNOSDisplayConnMode exynosDisplayGetDispConnMode(ScrnInfoPtr pScrn);

Bool exynosDisplayInitDispMode(ScrnInfoPtr pScrn,
                               EXYNOSDisplayConnMode conn_mode);
void exynosDisplayDeinitDispMode(ScrnInfoPtr pScrn);

Bool exynosDisplayGetCurMSC(ScrnInfoPtr pScrn, intptr_t pipe, CARD64 * ust,
                            CARD64 * msc);
Bool exynosDisplayVBlank(ScrnInfoPtr pScrn, intptr_t pipe, CARD64 * target_msc,
                         intptr_t flip, EXYNOSVBlankInfoType type,
                         void *vblank_info);
int exynosDisplayDrawablePipe(DrawablePtr pDraw);

intptr_t exynosDisplayCrtcPipe(ScrnInfoPtr pScrn, int crtc_id);

Bool exynosDisplayUpdateRequest(ScrnInfoPtr pScrn);

#ifdef NO_CRTC_MODE
Bool exynosDisplayChangeMode(ScrnInfoPtr pScrn);
#endif
#endif                          /* __SEC_DISPLAY_H__ */
