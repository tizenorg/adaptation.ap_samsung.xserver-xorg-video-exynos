/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

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

#ifndef _SEC_ACCEL_H_
#define _SEC_ACCEL_H_ 1

#include <exa.h>
#include <dri2.h>
#include <list.h>
#include <picture.h>
#include <xf86drm.h>
#include <tbm_bufmgr.h>
#include <damage.h>

#include "sec_layer.h"

/* exa driver private infomation */
typedef struct _secExaPriv SECExaPriv, *SECExaPrivPtr;
#define SECEXAPTR(p) ((SECExaPrivPtr)((p)->pExaPriv))

/* pixmap usage hint */
#define CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK  0x100
#define CREATE_PIXMAP_USAGE_FB              0x101
#define CREATE_PIXMAP_USAGE_SUB_FB          0x202
#define CREATE_PIXMAP_USAGE_DRI2_BACK       0x404
#define CREATE_PIXMAP_USAGE_DRI3_BACK       0x606
#define CREATE_PIXMAP_USAGE_XVIDEO          0x808

typedef struct
{
    int usage_hint;
    long size;

    /* buffer object */
    tbm_bo bo;
    pointer pPixData;   /*text glyphs or SHM-PutImage*/

    int isFrameBuffer;
    int isSubFramebuffer;

    SECLayer *ovl_layer;

    /* for exa operation */
    void* exaOpInfo;

    /* dump count */
    int dump_cnt;

    /* Last update SBC */
    XID owner;
    CARD64 sbc;

    /* for DRI3 */
    int stride;
    WindowPtr pWin;
} SECPixmapPriv;

/* exa driver private infomation */
struct _secExaPriv
{
    ExaDriverPtr pExaDriver;

    int flip_backbufs;
};

/* type of the frame event */
typedef enum _dri2FrameEventType
{
    DRI2_NONE,
    DRI2_SWAP,
    DRI2_FLIP,
    DRI2_BLIT,
    DRI2_FB_BLIT,
    DRI2_WAITMSC,
} DRI2FrameEventType;

/* dri2 frame event information */
typedef struct _dri2FrameEvent
{
    DRI2FrameEventType type;
    XID drawable_id;
    unsigned int client_idx;
    ClientPtr pClient;
    int frame;

    /* for swaps & flips only */
    DRI2SwapEventPtr event_complete;
    void *event_data;
    DRI2BufferPtr pFrontBuf;
    DRI2BufferPtr pBackBuf;

    /* pending flip event */
    int crtc_pipe;
    void *pCrtc;
    struct _dri2FrameEvent *pPendingEvent;
    struct xorg_list crtc_pending_link;

    /* for SwapRegion */
    RegionPtr pRegion;
} DRI2FrameEventRec, *DRI2FrameEventPtr;

/**************************************************************************
 * EXA
 **************************************************************************/
/* EXA */
Bool secExaInit                 (ScreenPtr pScreen);
void secExaDeinit               (ScreenPtr pScreen);
Bool secExaPrepareAccess        (PixmapPtr pPix, int index);
void secExaFinishAccess         (PixmapPtr pPix, int index);
Bool secExaMigratePixmap        (PixmapPtr pPix, tbm_bo bo);
void secExaScreenCountFps       (ScreenPtr pScreen); /* count fps */
void secExaScreenLock           (ScreenPtr pScreen, int enable);
int  secExaScreenAsyncSwap      (ScreenPtr pScreen, int enable);
int  secExaScreenSetScrnPixmap  (ScreenPtr pScreen);
tbm_bo secExaPixmapGetBo    (PixmapPtr pPix);
int secExaPixmapSetBo(PixmapPtr pPix, tbm_bo bo);

/* sw EXA */
Bool secExaSwInit   (ScreenPtr pScreen, ExaDriverPtr pExaDriver);
void secExaSwDeinit (ScreenPtr pScreen);
Bool secExaG2dInit   (ScreenPtr pScreen, ExaDriverPtr pExaDriver);
void secExaG2dDeinit (ScreenPtr pScreen);


/**************************************************************************
 * Present
 **************************************************************************/
/* Present */
Bool secPresentScreenInit  		(ScreenPtr pScreen);
/* Present event handlers (vblank, pageflip) */
void secPresentVblankHandler	(unsigned int frame, unsigned int tv_sec, unsigned int tv_usec, void* event_data);
void secPresentFlipEventHandler	(unsigned int frame, unsigned int tv_sec, unsigned int tv_usec, void* event_data, Bool flip_failed);
void secPresentVblankAbort		(ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void *data);
void secPresentFlipAbort		(void* pageflip_data);

/**************************************************************************
 * DRI2
 **************************************************************************/
/* DRI2 */
Bool secDri2Init              (ScreenPtr pScreen);
void secDri2Deinit            (ScreenPtr pScreen);
void secDri2FrameEventHandler (unsigned int frame, unsigned int tv_sec, unsigned int tv_usec, void *event_data);
void secDri2FlipEventHandler  (unsigned int frame, unsigned int tv_sec, unsigned int tv_usec, void *event_data, Bool flip_failed);
void secDri2ProcessPending    (xf86CrtcPtr pCrtc, unsigned int frame, unsigned int tv_sec, unsigned int tv_usec);

/**************************************************************************
 * DRI3
 **************************************************************************/
/* DRI3 */
Bool secDri3ScreenInit	  (ScreenPtr screen);


#endif /* _SEC_ACCEL_H_ */
