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

#ifndef __SEC_CRTC_H__
#define __SEC_CRTC_H__

#include "sec_display.h"
#include "sec_layer.h"
#include "sec_util.h"

typedef enum
{
    ACCESSIBILITY_MODE_NONE,
    ACCESSIBILITY_MODE_NEGATIVE,
}ACCESSIBILITY_STATUS;

typedef struct _secCrtcPriv
{
    SECModePtr pSecMode;
    drmModeModeInfo kmode;
    drmModeCrtcPtr mode_crtc;
    intptr_t pipe;

    int idx;
    tbm_bo front_bo;
    tbm_bo back_bo;

    /* for pageflip */
    unsigned int fe_frame;
    unsigned int fe_tv_sec;
    unsigned int fe_tv_usec;
    DRI2FrameEventPtr flip_info; /* pending flips : flipping must garauntee to do it sequentially */
    struct xorg_list pending_flips;
    Bool is_flipping;  /* check flipping */
    Bool is_fb_blit_flipping;
    int flip_count;    /* check flipping completed (check pairs of request_flip and complete_flip */
    struct {
        int num;        /* number of flip back pixmaps */
        int lub;		/* Last used backbuffer */
        Bool *pix_free;   /* flags for a flip pixmap to be free */
        DrawablePtr *flip_draws;
        PixmapPtr *flip_pixmaps; /* back flip pixmaps in a crtc */
    } flip_backpixs;

#if 1
    /* for fps debug */
    FpsDebugPtr pFpsDebug;
#endif

    /* overlay(cursor) */
    Bool need_off;
    Bool ref_overlay;
    Bool move_layer;
    Bool cursor_show;
    Bool need_draw_cursor;
    SECLayer  *ovl_layer;
    SECVideoBuf *ovl_vbuf_cursor;
    SECVideoBuf *ovl_vbuf_pixmap;
    Bool need_cursor_update;
    Bool registered_block_handler;
    int user_rotate;
    int cursor_old_offset;
    int cursor_pos_x;
    int cursor_pos_y;
    int cursor_win_x;
    int cursor_win_y;
    BoxRec saved_box;
    pixman_image_t *ovl_canvas;
    pixman_image_t *saved_image;
    pixman_image_t *cursor_image;
    pixman_image_t *backup_image;

    tbm_bo rotate_bo;
    uint32_t rotate_pitch;
    uint32_t rotate_fb_id;

    /* crtc rotate by display conf */
    Rotation rotate;

    /* Accessibility */
    tbm_bo accessibility_front_bo;
    tbm_bo accessibility_back_bo;
    Bool bAccessibility;
    ACCESSIBILITY_STATUS accessibility_status;
    Bool bScale;
    int sx, sy, sw, sh;

    /* screen rotate */
    int    screen_rotate_degree;
    int    screen_rotate_prop_id;
    int    screen_rotate_ipp_status;

    Bool onoff;
    Bool onoff_always;

    xf86CrtcPtr pCrtc;
    Bool is_dummy;
    struct xorg_list link;
} SECCrtcPrivRec, *SECCrtcPrivPtr;
#if 0
xf86CrtcPtr    secCrtcDummyInit (ScrnInfoPtr pScrn);
#endif
Bool    secCrtcCheckInUseAll(ScrnInfoPtr pScrn);
void    secCrtcInit  (ScrnInfoPtr pScrn, SECModePtr pSecMode, int num);
Bool    secCrtcOn    (xf86CrtcPtr pCrtc);
Bool    secCrtcApply (xf86CrtcPtr pCrtc);

Bool    secCrtcOverlayNeedOff    (xf86CrtcPtr pCrtc, Bool need_off);
Bool    secCrtcOverlayRef    (xf86CrtcPtr pCrtc, Bool refer);
Bool    secCrtcCursorEnable  (ScrnInfoPtr pScrn, Bool enable);
Bool    secCrtcCursorRotate  (xf86CrtcPtr pCrtc, int rotate);

Bool    secCrtcScreenRotate (xf86CrtcPtr pCrtc, int degree);
Bool    secCrtcEnableScreenRotate (xf86CrtcPtr pCrtc, Bool enable);

xf86CrtcPtr secCrtcGetAtGeometry     (ScrnInfoPtr pScrn, int x, int y, int width, int height);
int         secCrtcGetConnectType    (xf86CrtcPtr pCrtc);

Bool secCrtcIsFlipping (xf86CrtcPtr pCrtc);
void secCrtcAddPendingFlip (xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent);
void secCrtcRemovePendingFlip (xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent);
DRI2FrameEventPtr secCrtcGetPendingFlip (xf86CrtcPtr pCrtc, DRI2FrameEventPtr pEvent);
DRI2FrameEventPtr secCrtcGetFirstPendingFlip (xf86CrtcPtr pCrtc);

Bool    secCrtcEnableAccessibility (xf86CrtcPtr pCrtc);
Bool    secCrtcExecAccessibility   (xf86CrtcPtr pCrtc, tbm_bo src_bo, tbm_bo dst_bo);

Bool    secCrtcTurn (xf86CrtcPtr pCrtc, Bool onoff, Bool always, Bool user);
Bool    secCrtcCheckOn (xf86CrtcPtr pCrtc);

Bool      secCrtcFullFreeFlipPixmap (ScrnInfoPtr pScrn, int crtc_pipe);
PixmapPtr secCrtcGetFreeFlipPixmap (ScrnInfoPtr pScrn, int crtc_pipe, DrawablePtr pDraw, unsigned int usage_hint);
void      secCrtcRelFlipPixmap (ScrnInfoPtr pScrn, int crtc_pipe, PixmapPtr pPixmap);
void      secCrtcRelAllFlipPixmap (ScrnInfoPtr pScrn, int crtc_pipe);
void      secCrtcRemoveFlipPixmap (xf86CrtcPtr pCrtc);

void    secCrtcCountFps(xf86CrtcPtr pCrtc);

xf86CrtcPtr secCrtcGetByID(ScrnInfoPtr pScrn, int crtc_id);

static inline int
secCrtcID(SECCrtcPrivPtr pCrtcPriv)
{
    return pCrtcPriv->mode_crtc->crtc_id;
}


#endif /* __SEC_CRTC_H__ */

