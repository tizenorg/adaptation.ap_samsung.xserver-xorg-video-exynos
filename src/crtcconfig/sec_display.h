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

typedef enum
{
    DISPLAY_SET_MODE_OFF,
    DISPLAY_SET_MODE_CLONE,
    DISPLAY_SET_MODE_EXT,
} SECDisplaySetMode;

typedef enum
{
    DISPLAY_CONN_MODE_NONE,
    DISPLAY_CONN_MODE_HDMI,
    DISPLAY_CONN_MODE_VIRTUAL,
    DISPLAY_CONN_MODE_MAX,
} SECDisplayConnMode;

typedef enum
{
    VBLNAK_INFO_NONE,
    VBLANK_INFO_SWAP,
    VBLANK_INFO_PLANE,
    VBLANK_INFO_MAX
} SECVBlankInfoType;

typedef struct _secDrmEventContext {
    void (*vblank_handler) (int fd,
                            unsigned int sequence,
                            unsigned int tv_sec,
                            unsigned int tv_usec,
                            void *user_data);

    void (*page_flip_handler) (int fd,
                               unsigned int sequence,
                               unsigned int tv_sec,
                               unsigned int tv_usec,
                               void *user_data);

    void (*g2d_handler) (int fd,
                         unsigned int cmdlist_no,
                         unsigned int tv_sec,
                         unsigned int tv_usec,
                         void *user_data);

    void (*ipp_handler) (int fd,
                         unsigned int  prop_id,
                         unsigned int *buf_idx,
                         unsigned int  tv_sec,
                         unsigned int  tv_usec,
                         void *user_data);
} secDrmEventContext, *secDrmEventContextPtr;

typedef struct _secDrmMode
{
    int type;
    int fd;
    drmModeResPtr mode_res;
    drmModePlaneResPtr plane_res;
    int cpp;
    drmModeModeInfo main_lcd_mode;
    drmModeModeInfo ext_connector_mode;

    secDrmEventContext event_context;

    struct xorg_list outputs;
    struct xorg_list crtcs;
    struct xorg_list planes;

    SECDisplaySetMode  set_mode;
    SECDisplayConnMode conn_mode;
    int                rotate;

    int unset_connector_type;
} SECModeRec, *SECModePtr;

typedef struct _secPageFlip
{
    xf86CrtcPtr pCrtc;
    Bool dispatch_me;
    Bool clone;
    Bool flip_failed;

    tbm_bo back_bo;
    tbm_bo accessibility_back_bo;

    void *data;
    CARD32 time;

#if DBG_DRM_EVENT
    void *xdbg_log_pageflip;
#endif
} SECPageFlipRec, *SECPageFlipPtr;

typedef struct _secVBlankInfo
{
    SECVBlankInfoType type;
    void *data; /* request data pointer */
    CARD32 time;

#if DBG_DRM_EVENT
    void *xdbg_log_vblank;
#endif
} SECVBlankInfoRec, *SECVBlankInfoPtr;

typedef struct _secProperty
{
    drmModePropertyPtr mode_prop;
    uint64_t value;
    int num_atoms; /* if range prop, num_atoms == 1; if enum prop, num_atoms == num_enums + 1 */
    Atom *atoms;
} SECPropertyRec, *SECPropertyPtr;

Bool        secModePreInit (ScrnInfoPtr pScrn, int drm_fd);
void        secModeInit (ScrnInfoPtr pScrn);
void        secModeDeinit (ScrnInfoPtr pScrn);
xf86CrtcPtr secModeCoveringCrtc (ScrnInfoPtr pScrn, BoxPtr pBox, xf86CrtcPtr pDesiredCrtc, BoxPtr pBoxCrtc);
int         secModeGetCrtcPipe (xf86CrtcPtr pCrtc);
Bool        secModePageFlip (ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void* flip_info, int pipe, tbm_bo back_bo);
void        secModeLoadPalette (ScrnInfoPtr pScrn, int numColors, int* indices, LOCO* colors, VisualPtr pVisual);

void        secDisplaySwapModeFromKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode, DisplayModePtr	pMode);
void        secDisplayModeFromKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode, DisplayModePtr	pMode);
void        secDisplaySwapModeToKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode, DisplayModePtr pMode);
void        secDisplayModeToKmode(ScrnInfoPtr pScrn, drmModeModeInfoPtr kmode, DisplayModePtr pMode);

Bool               secDisplaySetDispSetMode  (ScrnInfoPtr pScrn, SECDisplaySetMode disp_mode);
SECDisplaySetMode  secDisplayGetDispSetMode  (ScrnInfoPtr pScrn);
Bool               secDisplaySetDispRotate   (ScrnInfoPtr pScrn, int disp_rotate);
int                secDisplayGetDispRotate   (ScrnInfoPtr pScrn);
Bool               secDisplaySetDispConnMode (ScrnInfoPtr pScrn, SECDisplayConnMode disp_conn);
SECDisplayConnMode secDisplayGetDispConnMode (ScrnInfoPtr pScrn);

Bool secDisplayInitDispMode (ScrnInfoPtr pScrn, SECDisplayConnMode conn_mode);
void secDisplayDeinitDispMode (ScrnInfoPtr pScrn);

Bool secDisplayGetCurMSC (ScrnInfoPtr pScrn, int pipe, CARD64 *ust, CARD64 *msc);
Bool secDisplayVBlank (ScrnInfoPtr pScrn, int pipe, CARD64 *target_msc, int flip, SECVBlankInfoType type, void *vblank_info);
int secDisplayDrawablePipe (DrawablePtr pDraw);

int secDisplayCrtcPipe (ScrnInfoPtr pScrn, int crtc_id);

Bool secDisplayUpdateRequest(ScrnInfoPtr pScrn);

#endif /* __SEC_DISPLAY_H__ */

