/**************************************************************************

xserver-xorg-video-exynos

Copyright 2001 VA Linux Systems Inc., Fremont, California.
Copyright © 2002 by David Dawes
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>
#include <poll.h>

#include "X11/Xatom.h"
#include "xorg-server.h"
#include "xf86.h"
#include "dri2.h"
#include "damage.h"
#include "windowstr.h"
#include "exynos.h"
#include "exynos_accel.h"
#include "exynos_display.h"
#include "exynos_crtc.h"
#include "exynos_util.h"
#include "exynos_crtc.h"
#include "exynos_xberc.h"
#include "exynos_layer_manager.h"
#include "exynos_hwc.h"

#define DRI2_BUFFER_TYPE_WINDOW 0x0
#define DRI2_BUFFER_TYPE_PIXMAP 0x1
#define DRI2_BUFFER_TYPE_FB     0x2

typedef union {
    unsigned int flags;
    struct {
        unsigned int type:1;
        unsigned int is_framebuffer:1;
        unsigned int is_viewable:1;
        unsigned int is_reused:1;
        unsigned int idx_reuse:3;
    } data;
} DRI2BufferFlags;

#define DRI2_GET_NEXT_IDX(idx, max) (((idx+1) % (max)))

/* if a window is mapped and realized (viewable) */
#define IS_VIEWABLE(pDraw) \
    ((pDraw->type == DRAWABLE_PIXMAP)?TRUE:(Bool)(((WindowPtr) pDraw)->viewable))

/* dri2 buffer private infomation */
typedef struct _dri2BufferPriv {
    int refcnt;
    int attachment;
    PixmapPtr pPixmap;
    ScreenPtr pScreen;

    /* pixmap of the backbuffer */
    int pipe;
    Bool canFlip;
    int num_buf;
    int avail_idx;              /* next available index of the back pixmap, -1 means not to be available */
    int cur_idx;                /* current index of the back pixmap, -1 means not to be available */
    int free_idx;               /* free index of the back pixmap, -1 means not to be available */
    PixmapPtr *pBackPixmaps;

    /* flip buffers */
    ClientPtr pClient;
    DRI2FrameEventPtr pFlipEvent;
} DRI2BufferPrivRec, *DRI2BufferPrivPtr;

/* prototypes */
static void EXYNOSDri2CopyRegion(DrawablePtr pDraw, RegionPtr pRegion,
                                 DRI2BufferPtr pDstBuf, DRI2BufferPtr pSrcBuf);
static void EXYNOSDri2DestroyBuffer(DrawablePtr pDraw, DRI2BufferPtr pBuf);

static PixmapPtr _initBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw,
                                    Bool canFlip);
static void _deinitBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw,
                                 Bool canFlip);
static void _exchangeBackBufPixmap(DRI2BufferPtr pBackBuf);
static void _disuseBackBufPixmap(DRI2BufferPtr pBackBuf,
                                 DRI2FrameEventPtr pEvent);
static PixmapPtr _reuseBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw,
                                     Bool canFlip, int *reues);
static Bool _scheduleLayerFlip(DrawablePtr pDraw, DRI2FrameEventPtr pEvent);
static void _bo_swap(tbm_bo back_bo, tbm_bo front_bo);

#ifdef LAYER_MANAGER
static EXYNOSLayerMngClientID lyr_client_id = 0;
#endif

static unsigned int
_getName(PixmapPtr pPix)
{
    EXYNOSPixmapPriv *pExaPixPriv = NULL;

    if (pPix == NULL)
        return 0;

    pExaPixPriv = exaGetPixmapDriverPrivate(pPix);
    if (pExaPixPriv == NULL)
        return 0;

    if (pExaPixPriv->bo == NULL) {
        if (pExaPixPriv->isFrameBuffer)
            return (unsigned int) ROOT_FB_ADDR;
        else
            return 0;
    }

    return tbm_bo_export(pExaPixPriv->bo);
}

static PixmapPtr *
_createBackPixmaps(DrawablePtr pDraw, DRI2BufferPtr pBuf, Bool flip)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;
    int i;

#ifdef SIMPLE_DRI2
    pBufPriv->num_buf = 1;
#else
    if (flip)
        pBufPriv->num_buf = EXYNOSEXAPTR(pExynos)->flip_backbufs;
    else
        pBufPriv->num_buf = 1;
#endif

    pBufPriv->pBackPixmaps =
        calloc(EXYNOSEXAPTR(pExynos)->flip_backbufs, sizeof(void *));
    XDBG_RETURN_VAL_IF_FAIL(pBufPriv->pBackPixmaps != NULL, NULL);

    for (i = 0; i < pBufPriv->num_buf; i++) {
        pBufPriv->pBackPixmaps[i] =
            (*pScreen->CreatePixmap) (pScreen, pDraw->width, pDraw->height,
                                      pDraw->depth,
                                      CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK);
        XDBG_GOTO_IF_FAIL(pBufPriv->pBackPixmaps[i] != NULL, fail);
#if USE_XDBG
        xDbgLogPListDrawAddRefPixmap(pDraw, pBufPriv->pBackPixmaps[i]);
#endif

        EXYNOSPixmapPriv *pPixPriv =
            exaGetPixmapDriverPrivate(pBufPriv->pBackPixmaps[i]);
        XDBG_DEBUG(MDRI2, "[%p] Allocate Backbuffer(%d): pix=%p name=%d\n",
                   (void *) pDraw->id, i, pBufPriv->pBackPixmaps[i],
                   tbm_bo_export(pPixPriv->bo));
    }

    pBufPriv->canFlip = flip;
    pBufPriv->avail_idx = 0;
    pBufPriv->free_idx = 0;
    pBufPriv->cur_idx = 0;
    pBufPriv->pipe = 0;         //at this case it is unimportant

    return pBufPriv->pBackPixmaps;

 fail:
    for (i = 0; i < pBufPriv->num_buf; i++) {
        if (pBufPriv->pBackPixmaps[i] != NULL) {
            xDbgLogPListDrawRemoveRefPixmap(pDraw, pBufPriv->pBackPixmaps[i]);
            (*pScreen->DestroyPixmap) (pBufPriv->pBackPixmaps[i]);
            pBufPriv->pBackPixmaps[i] = NULL;
        }
    }

    if (pBufPriv->pBackPixmaps) {
        free(pBufPriv->pBackPixmaps);
        pBufPriv->pBackPixmaps = NULL;
    }

    return NULL;
}

static PixmapPtr
_getBackPixmap(DrawablePtr pDraw, DRI2BufferPtr pBuf, Bool flip)
{
    ScreenPtr pScreen = pDraw->pScreen;
    DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;

    PixmapPtr pPixmap = NULL;
    EXYNOSPixmapPriv *pPixPriv = NULL;

    if (!pBufPriv->pBackPixmaps) {
        XDBG_RETURN_VAL_IF_FAIL(_createBackPixmaps(pDraw, pBuf, flip), NULL);
        XDBG_RETURN_VAL_IF_FAIL(pBufPriv->pBackPixmaps != NULL, NULL);
    }
    else {
        //increase the number of back buffers when SWAP->FLIP, but never decrease
        if (flip && pBufPriv->num_buf < 2)
            pBufPriv->num_buf = 2;
    }

    if (pBufPriv->pBackPixmaps[pBufPriv->avail_idx] == NULL) {
        pBufPriv->pBackPixmaps[pBufPriv->avail_idx] =
            (*pScreen->CreatePixmap) (pScreen, pDraw->width, pDraw->height,
                                      pDraw->depth,
                                      CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK);
        XDBG_RETURN_VAL_IF_FAIL(pBufPriv->pBackPixmaps[pBufPriv->avail_idx] !=
                                NULL, NULL);
#if USE_XDBG
        xDbgLogPListDrawAddRefPixmap(pDraw, pBufPriv->pBackPixmaps[pBufPriv->avail_idx]);
#endif

        pPixmap = pBufPriv->pBackPixmaps[pBufPriv->avail_idx];

        pPixPriv =
            exaGetPixmapDriverPrivate(pBufPriv->pBackPixmaps
                                      [pBufPriv->avail_idx]);
        XDBG_DEBUG(MDRI2, "[%p] Allocate Backbuffer(%d): pix=%p name=%d\n",
                   (void *) pDraw->id, pBufPriv->avail_idx,
                   pBufPriv->pBackPixmaps[pBufPriv->avail_idx],
                   tbm_bo_export(pPixPriv->bo));
    }
    else {
        pPixmap = pBufPriv->pBackPixmaps[pBufPriv->avail_idx];

        pPixPriv = exaGetPixmapDriverPrivate(pPixmap);
        XDBG_DEBUG(MDRI2, "[%p] Get Backbuffer(%d): pix=%p name=%d\n",
                   (void *) pDraw->id, pBufPriv->avail_idx, pPixmap,
                   tbm_bo_export(pPixPriv->bo));
    }

    return pPixmap;
}

/* initialize the pixmap of the backbuffer */
static PixmapPtr
_initBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw, Bool canFlip)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSExaPrivPtr pExaPriv = EXYNOSEXAPTR(pExynos);
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;
    unsigned int usage_hint = CREATE_PIXMAP_USAGE_DRI2_BACK;
    PixmapPtr pPixmap = NULL;
    int pipe = -1;

    /* HWComposite */
    if (pExynos->hwc_active) {
        return _getBackPixmap(pDraw, pBackBuf, canFlip);
    }

    /* if a drawable can be flip, check whether the flip buffer is available */
    if (canFlip) {
        usage_hint = CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK;
        pipe = exynosDisplayDrawablePipe(pDraw);
        if (pipe != -1) {
            /* get the flip pixmap from crtc */
            pPixmap =
                exynosCrtcGetFreeFlipPixmap(pScrn, pipe, pDraw, usage_hint);
            if (!pPixmap) {
                /* fail to get a  flip pixmap from crtc */
                canFlip = FALSE;
                XDBG_WARNING(MDRI2, "fail to get a  flip pixmap from crtc\n");
            }
        }
        else {
            /* pipe is -1 */
            canFlip = FALSE;
#ifndef NO_CRTC_MODE
            XDBG_WARNING(MDRI2, "pipe is -1\n");
#endif
        }
    }

    /* if canflip is false, get the dri2_back pixmap */
    if (!canFlip) {
        pPixmap = (*pScreen->CreatePixmap) (pScreen,
                                            pDraw->width,
                                            pDraw->height,
                                            pDraw->depth, usage_hint);
        XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
#if USE_XDBG
        xDbgLogPListDrawAddRefPixmap(pDraw, pPixmap);
#endif
    }

    if (canFlip) {
        pBackBufPriv->num_buf = pExaPriv->flip_backbufs;
        pBackBufPriv->pBackPixmaps =
            calloc(pBackBufPriv->num_buf, sizeof(void *));
    }
    else {
        pBackBufPriv->num_buf = 1;      /* num of backbuffer for swap/blit */
        pBackBufPriv->pBackPixmaps =
            calloc(pBackBufPriv->num_buf, sizeof(void *));
    }

    XDBG_RETURN_VAL_IF_FAIL((pBackBufPriv->pBackPixmaps != NULL), NULL);

    pBackBufPriv->pBackPixmaps[0] = pPixmap;
    pBackBufPriv->canFlip = canFlip;
    pBackBufPriv->avail_idx = 0;
    pBackBufPriv->free_idx = 0;
    pBackBufPriv->cur_idx = 0;
    pBackBufPriv->pipe = pipe;

    return pPixmap;
}

/* deinitialize the pixmap of the backbuffer */
static void
_deinitBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw, Bool canFlip)
{
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;
    ScreenPtr pScreen = pBackBufPriv->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int i;
    int pipe = -1;

    for (i = 0; i < pBackBufPriv->num_buf; i++) {
        if (pBackBufPriv->pBackPixmaps) {
            if (pBackBufPriv->pBackPixmaps[i]) {
                if (canFlip && (!EXYNOSPTR(pScrn)->hwc_active)) {
                    /* have to release the flip pixmap */
                    pipe = pBackBufPriv->pipe;
                    if (pipe != -1)
                        exynosCrtcRelAllFlipPixmap(pScrn, pipe);
                    else
                        XDBG_WARNING(MDRI2, "pipe is -1\n");
                }
                else {
#if USE_XDBG
                    xDbgLogPListDrawRemoveRefPixmap(pDraw,
                                                    pBackBufPriv->pBackPixmaps
                                                    [i]);
#endif
                    (*pScreen->DestroyPixmap) (pBackBufPriv->pBackPixmaps[i]);
                }
                pBackBufPriv->pBackPixmaps[i] = NULL;
                pBackBufPriv->pPixmap = NULL;
            }
        }
    }
    free(pBackBufPriv->pBackPixmaps);
    pBackBufPriv->pBackPixmaps = NULL;
}

/* increase the next available index of the backbuffer */
static void
_exchangeBackBufPixmap(DRI2BufferPtr pBackBuf)
{
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;

    /* increase avail_idx when buffers exchange */
    pBackBufPriv->avail_idx =
        DRI2_GET_NEXT_IDX(pBackBufPriv->avail_idx, pBackBufPriv->num_buf);
}

/* return the next available pixmap of the backbuffer */
static PixmapPtr
_reuseBackBufPixmap(DRI2BufferPtr pBackBuf, DrawablePtr pDraw, Bool canFlip,
                    int *reues)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;
    PixmapPtr pPixmap = NULL;
    int avail_idx = pBackBufPriv->avail_idx;
    unsigned int usage_hint = CREATE_PIXMAP_USAGE_DRI2_BACK;
    int pipe = -1;

    if (EXYNOSPTR(pScrn)->hwc_active) {
        *reues = 1;
        pPixmap = _getBackPixmap(pDraw, pBackBuf, canFlip);
        pBackBufPriv->canFlip = canFlip;
        return pPixmap;
    }

    if (pBackBufPriv->canFlip != canFlip) {
        /* flip buffer -> swap buffer */
        if (pBackBufPriv->canFlip && !canFlip) {
            /* return the next available pixmap */
            _deinitBackBufPixmap(pBackBuf, pDraw, pBackBufPriv->canFlip);
            pPixmap = _initBackBufPixmap(pBackBuf, pDraw, canFlip);
            XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
            return pPixmap;
        }

        /* swap buffer -> flip buffer */
        if (!pBackBufPriv->canFlip && canFlip) {
            pipe = exynosDisplayDrawablePipe(pDraw);
            if (pipe != -1) {
                /* return the next available pixmap */
                _deinitBackBufPixmap(pBackBuf, pDraw, pBackBufPriv->canFlip);
                pPixmap = _initBackBufPixmap(pBackBuf, pDraw, canFlip);
                XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
                return pPixmap;
            }
            else {
                canFlip = FALSE;
                XDBG_WARNING(MDRI2, "pipe is -1\n");
            }
        }
    }

    /* set the next available pixmap */
    /* if pBackPixmap is available, reuse it */
    if (pBackBufPriv->pBackPixmaps[avail_idx]) {
        if (canFlip) {
            usage_hint = CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK;
            pipe = exynosDisplayDrawablePipe(pDraw);
            if (pipe != -1) {
                if (avail_idx != pBackBufPriv->cur_idx) {
                    /* get the flip pixmap from crtc */
                    pBackBufPriv->pBackPixmaps[avail_idx] =
                        exynosCrtcGetFreeFlipPixmap(pScrn, pipe, pDraw,
                                                    usage_hint);
                    if (!pBackBufPriv->pBackPixmaps[avail_idx]) {
                        /* fail to get a  flip pixmap from crtc */
                        XDBG_WARNING(MDRI2,
                                     "@@[reuse]: draw(0x%x) fail to get a flip pixmap from crtc to reset the index of pixmap\n",
                                     (unsigned int) pDraw->id);

                        _deinitBackBufPixmap(pBackBuf, pDraw,
                                             pBackBufPriv->canFlip);
                        pPixmap = _initBackBufPixmap(pBackBuf, pDraw, FALSE);
                        XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
                        *reues = 0;
                        return pPixmap;
                    }
                    pBackBufPriv->cur_idx = avail_idx;
                }
            }
            else {
                XDBG_WARNING(MDRI2, "pipe is -1(%d)\n", pipe);
                return NULL;
            }
        }
        else {
            if (avail_idx != pBackBufPriv->cur_idx) {
                pBackBufPriv->cur_idx = avail_idx;
            }
        }

        *reues = 1;
    }
    else {
        if (canFlip) {
            usage_hint = CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK;
            pipe = exynosDisplayDrawablePipe(pDraw);
            if (pipe != -1) {
                if (avail_idx != pBackBufPriv->cur_idx) {
                    /* get the flip pixmap from crtc */
                    pBackBufPriv->pBackPixmaps[avail_idx] =
                        exynosCrtcGetFreeFlipPixmap(pScrn, pipe, pDraw,
                                                    usage_hint);
                    if (!pBackBufPriv->pBackPixmaps[avail_idx]) {
                        /* fail to get a  flip pixmap from crtc */
                        XDBG_WARNING(MDRI2,
                                     "@@[initial set]: draw(0x%x) fail to get a flip pixmap from crtc to generate and to set the next available pixmap.\n",
                                     (unsigned int) pDraw->id);

                        _deinitBackBufPixmap(pBackBuf, pDraw, TRUE);
                        pPixmap = _initBackBufPixmap(pBackBuf, pDraw, FALSE);
                        XDBG_RETURN_VAL_IF_FAIL(pPixmap != NULL, NULL);
                        *reues = 0;
                        return pPixmap;
                    }
                    pBackBufPriv->cur_idx = avail_idx;
                }
            }
        }
        else {
            if (avail_idx != pBackBufPriv->cur_idx) {

                pBackBufPriv->pBackPixmaps[avail_idx] =
                    (*pScreen->CreatePixmap) (pScreen, pDraw->width,
                                              pDraw->height, pDraw->depth,
                                              usage_hint);
                XDBG_RETURN_VAL_IF_FAIL(pBackBufPriv->pBackPixmaps[avail_idx] !=
                                        NULL, NULL);
                pBackBufPriv->cur_idx = avail_idx;
#if USE_XDBG
                xDbgLogPListDrawAddRefPixmap(pDraw, pPixmap);
#endif
            }
        }

        *reues = 0;
    }
    pPixmap = pBackBufPriv->pBackPixmaps[avail_idx];

    pBackBufPriv->canFlip = canFlip;
    return pPixmap;
}

static void
_disuseBackBufPixmap(DRI2BufferPtr pBackBuf, DRI2FrameEventPtr pEvent)
{
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;
    ScreenPtr pScreen = pBackBufPriv->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    if (pEvent->type == DRI2_FLIP) {
        if (!EXYNOSPTR(pScrn)->hwc_active) {
            exynosCrtcRelFlipPixmap(pScrn, pEvent->crtc_pipe,
                                    pBackBufPriv->pBackPixmaps[pBackBufPriv->
                                                               free_idx]);
        }
        /* increase free_idx when buffers destory or when frame is deleted */
        pBackBufPriv->free_idx =
            DRI2_GET_NEXT_IDX(pBackBufPriv->free_idx, pBackBufPriv->num_buf);
    }
}

static void
_setDri2Property(DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW) {
        static Atom atom_use_dri2 = 0;
        static int use = 1;

        if (!atom_use_dri2) {
            atom_use_dri2 = MakeAtom("X_WIN_USE_DRI2", 14, TRUE);
        }

        dixChangeWindowProperty(serverClient,
                                (WindowPtr) pDraw, atom_use_dri2, XA_CARDINAL,
                                32, PropModeReplace, 1, &use, TRUE);
    }
}

static unsigned int
_getBufferFlag(DrawablePtr pDraw, Bool canFlip)
{
    DRI2BufferFlags flag;

    flag.flags = 0;

    switch (pDraw->type) {
    case DRAWABLE_WINDOW:
        flag.data.type = DRI2_BUFFER_TYPE_WINDOW;
        break;
    case DRAWABLE_PIXMAP:
        flag.data.type = DRI2_BUFFER_TYPE_PIXMAP;
        break;
    }

    if (IS_VIEWABLE(pDraw)) {
        flag.data.is_viewable = 1;
    }

    if (canFlip) {
        flag.data.is_framebuffer = 1;
    }

    return flag.flags;
}

static inline PixmapPtr
_getPixmapFromDrawable(DrawablePtr pDraw)
{
    ScreenPtr pScreen = pDraw->pScreen;
    PixmapPtr pPix;

    if (pDraw->type == DRAWABLE_WINDOW)
        pPix = (*pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    else
        pPix = (PixmapPtr) pDraw;

    return pPix;
}

/* Can this drawable be page flipped? */
static Bool
_canFlip(DrawablePtr pDraw)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    WindowPtr pWin, pRoot;
    PixmapPtr pWinPixmap, pRootPixmap;
    int ret;

    if (!pExynos->use_flip)
        return FALSE;

    if (pDraw->type == DRAWABLE_PIXMAP)
        return FALSE;

    if (!IS_VIEWABLE(pDraw))
        return FALSE;
    if (pExynos->isLcdOff)
        return FALSE;
    if (pExynos->hwc_active && !pExynos->hwc_use_def_layer) {
        if (exynosHwcIsDrawExist(pDraw))
            return TRUE;
        return FALSE;
    }

    pRoot = pScreen->root;
    pRootPixmap = pScreen->GetWindowPixmap(pRoot);
    pWin = (WindowPtr) pDraw;
    pWinPixmap = pScreen->GetWindowPixmap(pWin);
    if (pRootPixmap != pWinPixmap)
        return FALSE;

    ret = exynosFbFindBo(pExynos->pFb,
                         pDraw->x, pDraw->y, pDraw->width, pDraw->height,
                         NULL, NULL);
    if (ret != rgnSAME)
        return FALSE;

    return TRUE;
}

static DRI2FrameEventType
_getSwapType(DrawablePtr pDraw, DRI2BufferPtr pFrontBuf, DRI2BufferPtr pBackBuf)
{
    DRI2BufferPrivPtr pFrontBufPriv;
    DRI2BufferPrivPtr pBackBufPriv;
    PixmapPtr pFrontPix;
    PixmapPtr pBackPix;
    EXYNOSPixmapPriv *pFrontExaPixPriv = NULL;
    EXYNOSPixmapPriv *pBackExaPixPriv = NULL;
    DRI2FrameEventType swap_type = DRI2_NONE;

    if (!pFrontBuf || !pBackBuf)
        return DRI2_NONE;

    /* if a buffer is not viewable at DRI2GetBuffers, return none */
    if (!IS_VIEWABLE(pDraw)) {
        //XDBG_WARNING(MDRI2, "DRI2_NONE: window is not viewable.(%d,%d)\n", pDraw->width, pDraw->height);
        return DRI2_NONE;
    }

    pFrontBufPriv = pFrontBuf->driverPrivate;
    pBackBufPriv = pBackBuf->driverPrivate;
    pFrontPix = pFrontBufPriv->pPixmap;
    pBackPix = pBackBufPriv->pPixmap;
    if (!pFrontPix || !pBackPix) {
        XDBG_WARNING(MDRI2,
                     "Warning: pFrontPix or pBackPix is null.(DRI2_NONE)\n");
        return DRI2_NONE;
    }

    pFrontExaPixPriv = exaGetPixmapDriverPrivate(pFrontBufPriv->pPixmap);
    pBackExaPixPriv = exaGetPixmapDriverPrivate(pBackBufPriv->pPixmap);
    if (!pFrontExaPixPriv || !pBackExaPixPriv) {
        XDBG_WARNING(MDRI2,
                     "Warning: pFrontPixPriv or pBackPixPriv is null.(DRI2_NONE)\n");
        return DRI2_NONE;
    }

    /* Check Exchange */
    if (pFrontBufPriv->canFlip == 1) {
        if (pBackBufPriv->canFlip == 1) {
            swap_type = DRI2_FLIP;

            if (!_canFlip(pDraw)) {
                ErrorF("@@@ [%10.3f] %" PRIXID " : flip to blit\n",
                       GetTimeInMillis() / 1000.0, pDraw->id);
                swap_type = DRI2_BLIT;
            }
        }
        else {
            XDBG_WARNING(MDRI2, "DRI2_FB_BLIT: Front(%d) Back(%d) \n",
                         pFrontBufPriv->canFlip, pBackBufPriv->canFlip);
            swap_type = DRI2_FB_BLIT;
        }
    }
    else {
        if (pFrontExaPixPriv->isFrameBuffer == 1) {
            //XDBG_WARNING (MDRI2, "DRI2_FB_BLIT: Front(%d) Back(%d) : front is framebuffer \n",
            //               pFrontBufPriv->canFlip, pBackBufPriv->canFlip);
            swap_type = DRI2_FB_BLIT;
        }
        else {
            if (pFrontPix->drawable.width == pBackPix->drawable.width &&
                pFrontPix->drawable.height == pBackPix->drawable.height &&
                pFrontPix->drawable.bitsPerPixel ==
                pBackPix->drawable.bitsPerPixel) {
                /*use swap only if flip is enable */
                ScrnInfoPtr pScrn = xf86ScreenToScrn(pDraw->pScreen);

                if (EXYNOSPTR(pScrn)->use_flip)
                    swap_type = DRI2_SWAP;
                else
                    swap_type = DRI2_BLIT;
            }
            else {
                swap_type = DRI2_BLIT;
            }
        }
    }

    return swap_type;
}

static void
_referenceBufferPriv(DRI2BufferPtr pBuf)
{
    if (pBuf) {
        DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;

        pBufPriv->refcnt++;
    }
}

static void
_unreferenceBufferPriv(DRI2BufferPtr pBuf)
{
    if (pBuf) {
        DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;

        pBufPriv->refcnt--;
    }
}

static Bool
_resetBufPixmap(DrawablePtr pDraw, DRI2BufferPtr pBuf)
{
    ScreenPtr pScreen = pDraw->pScreen;
    DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;
    PixmapPtr pPix = NULL, pNewPix = NULL, pOldPix = NULL;
    Bool canFlip = FALSE;
    int reuse = 0;

    canFlip = _canFlip(pDraw);

    if (pBufPriv->attachment == DRI2BufferFrontLeft) {
        pPix = _getPixmapFromDrawable(pDraw);
        if (pPix != pBufPriv->pPixmap ||
            ((DRI2BufferFlags) pBuf->flags).data.is_viewable !=
            IS_VIEWABLE(pDraw)) {
            pOldPix = pBufPriv->pPixmap;

            /* reset the pixmap and the name of the buffer */
            pNewPix = _getPixmapFromDrawable(pDraw);
            pPix->refcnt++;
            pBufPriv->canFlip = canFlip;

            /* Destroy Old buffer */
            if (pOldPix) {
                (*pScreen->DestroyPixmap) (pOldPix);
            }
        }
        else {
            pBufPriv->canFlip = canFlip;
            return FALSE;
        }
    }
    else {
        pNewPix = _reuseBackBufPixmap(pBuf, pDraw, canFlip, &reuse);
        if (pNewPix == NULL) {
            XDBG_WARNING(MDRI2, "Error pixmap is null\n");
            return FALSE;
        }

        if (reuse) {
            pBufPriv->pPixmap = pNewPix;
            return FALSE;
        }
    }

    pBufPriv->pPixmap = pNewPix;

    pBuf->name = _getName(pNewPix);
    pBuf->flags = _getBufferFlag(pDraw, canFlip);

    XDBG_TRACE(MDRI2,
               "id:0x%lx(%d) can_flip:%d attach:%d, name:%d, flags:0x%x geo(%dx%d+%d+%d)\n",
               pDraw->id, pDraw->type, pBufPriv->canFlip, pBuf->attachment,
               pBuf->name, pBuf->flags, pDraw->width, pDraw->height, pDraw->x,
               pDraw->y);

    return TRUE;
}

static void
_generateDamage(DrawablePtr pDraw, DRI2FrameEventPtr pFrameEvent)
{
    BoxRec box;
    RegionRec region;

    if (pFrameEvent->pRegion) {
        /* translate the regions with drawable */
        BoxPtr pBox = RegionRects(pFrameEvent->pRegion);
        int nBox = RegionNumRects(pFrameEvent->pRegion);

        while (nBox--) {
            box.x1 = pBox->x1;
            box.y1 = pBox->y1;
            box.x2 = pBox->x2;
            box.y2 = pBox->y2;
            XDBG_DEBUG(MDRI2,
                       "Damage Region[%d]: (x1, y1, x2, y2) = (%d,%d,%d,%d) \n ",
                       nBox, box.x1, box.x2, box.y1, box.y2);
            RegionInit(&region, &box, 0);
            DamageDamageRegion(pDraw, &region);
            pBox++;
        }
    }
    else {

        box.x1 = pDraw->x;
        box.y1 = pDraw->y;
        box.x2 = box.x1 + pDraw->width;
        box.y2 = box.y1 + pDraw->height;
        RegionInit(&region, &box, 0);
        DamageDamageRegion(pDraw, &region);
    }
}

static void
_blitBuffers(DrawablePtr pDraw, DRI2BufferPtr pFrontBuf, DRI2BufferPtr pBackBuf)
{
    BoxRec box;
    RegionRec region;

    box.x1 = 0;
    box.y1 = 0;
    box.x2 = pDraw->width;
    box.y2 = pDraw->height;
    REGION_INIT(pScreen, &region, &box, 0);

    EXYNOSDri2CopyRegion(pDraw, &region, pFrontBuf, pBackBuf);
}

static void
_exchangeBuffers(DrawablePtr pDraw, DRI2FrameEventType type,
                 DRI2BufferPtr pFrontBuf, DRI2BufferPtr pBackBuf)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    DRI2BufferPrivPtr pFrontBufPriv = pFrontBuf->driverPrivate;
    DRI2BufferPrivPtr pBackBufPriv = pBackBuf->driverPrivate;
    EXYNOSPixmapPriv *pFrontExaPixPriv =
        exaGetPixmapDriverPrivate(pFrontBufPriv->pPixmap);
    EXYNOSPixmapPriv *pBackExaPixPriv =
        exaGetPixmapDriverPrivate(pBackBufPriv->pPixmap);

//    if(pFrontBufPriv->canFlip != pBackBufPriv->canFlip)
//    {
//        XDBG_WARNING (MDRI2, "Cannot exchange buffer(0x%x): Front(%d, canFlip:%d), Back(%d, canFlip:%d)\n",
//                      (unsigned int)pDraw->id, pFrontBuf->name, pFrontBufPriv->canFlip,
//                      pBackBuf->name, pBackBufPriv->canFlip);
//
//        return;
//    }

    /* exchange the buffers
     * 1. exchange the bo of the exa pixmap private
     * 2. get the name of the front buffer (the name of the back buffer will get next DRI2GetBuffers.)
     */
    if (pFrontBufPriv->canFlip && !pExynos->hwc_active) {
        XDBG_RETURN_IF_FAIL(NULL !=
                            exynosFbSwapBo(pExynos->pFb, pBackExaPixPriv->bo));
        pFrontBuf->name = _getName(pFrontBufPriv->pPixmap);
    }
    else {
        //Front pixmap can be screen pixmap which doesn't have bo. In this case
        //exynosExaPrepareAccess() should be called.
        tbm_bo front_bo = pFrontExaPixPriv->bo;

        if (front_bo == NULL) {
            exynosExaPrepareAccess(pFrontBufPriv->pPixmap, EXA_PREPARE_DEST);
            front_bo = pFrontExaPixPriv->bo;
            exynosExaFinishAccess(pFrontBufPriv->pPixmap, EXA_PREPARE_DEST);
            XDBG_RETURN_IF_FAIL(front_bo != NULL);
        }
        _bo_swap(front_bo, pBackExaPixPriv->bo);
        pFrontBuf->name = _getName(pFrontBufPriv->pPixmap);
    }

    /*Exchange pixmap owner and sbc */
    {
        XID owner;
        CARD64 sbc;

        owner = pFrontExaPixPriv->owner;
        sbc = pFrontExaPixPriv->sbc;

        pFrontExaPixPriv->owner = pBackExaPixPriv->owner;
        pFrontExaPixPriv->sbc = pBackExaPixPriv->sbc;

        pBackExaPixPriv->owner = owner;
        pBackExaPixPriv->sbc = sbc;
    }

    /* exchange the index of the available buffer */
    _exchangeBackBufPixmap(pBackBuf);
}

static DRI2FrameEventPtr
_newFrame(ClientPtr pClient, DrawablePtr pDraw,
          DRI2BufferPtr pFrontBuf, DRI2BufferPtr pBackBuf,
          DRI2SwapEventPtr swap_func, void *data, RegionPtr pRegion)
{
    DRI2FrameEventPtr pFrameEvent = NULL;
    DRI2FrameEventType swap_type = DRI2_NONE;

    /* check and get the swap_type */
    swap_type = _getSwapType(pDraw, pFrontBuf, pBackBuf);
    if (swap_type == DRI2_NONE)
        return NULL;

    pFrameEvent = calloc(1, sizeof(DRI2FrameEventRec));
    if (!pFrameEvent)
        return NULL;

    pFrameEvent->type = swap_type;
    pFrameEvent->drawable_id = pDraw->id;
    pFrameEvent->client_idx = pClient->index;
    pFrameEvent->pClient = pClient;
    pFrameEvent->event_complete = swap_func;
    pFrameEvent->event_data = data;
    pFrameEvent->pFrontBuf = pFrontBuf;
    pFrameEvent->pBackBuf = pBackBuf;

    if (pRegion) {
        pFrameEvent->pRegion = RegionCreate(RegionExtents(pRegion),
                                            RegionNumRects(pRegion));
        if (!RegionCopy(pFrameEvent->pRegion, pRegion)) {
            RegionDestroy(pFrameEvent->pRegion);
            pFrameEvent->pRegion = NULL;
        }
    }
    else {
        pFrameEvent->pRegion = NULL;
    }

    _referenceBufferPriv(pFrontBuf);
    _referenceBufferPriv(pBackBuf);

    return pFrameEvent;
}

static void
_swapFrame(DrawablePtr pDraw, DRI2FrameEventPtr pFrameEvent)
{
    switch (pFrameEvent->type) {
    case DRI2_FLIP:
        //_generateDamage (pDraw, pFrameEvent);
        break;
    case DRI2_SWAP:
        _exchangeBuffers(pDraw, pFrameEvent->type,
                         pFrameEvent->pFrontBuf, pFrameEvent->pBackBuf);
        _generateDamage(pDraw, pFrameEvent);
        break;
    case DRI2_BLIT:
    case DRI2_FB_BLIT:
        /* copy the region from back buffer to front buffer */
        _blitBuffers(pDraw, pFrameEvent->pFrontBuf, pFrameEvent->pBackBuf);
        break;
    default:
        /* Unknown type */
        XDBG_WARNING(MDRI2, "%s: unknown swap_type received\n", __func__);
        _generateDamage(pDraw, pFrameEvent);
        break;
    }
}

static void
_deleteFrame(DrawablePtr pDraw, DRI2FrameEventPtr pEvent)
{
    /* some special case */
    DRI2BufferPrivPtr pFrontBufPriv;
    DRI2BufferPrivPtr pBackBufPriv;

    if (pEvent->pBackBuf && pEvent->pFrontBuf) {
        pFrontBufPriv = pEvent->pFrontBuf->driverPrivate;
        pBackBufPriv = pEvent->pBackBuf->driverPrivate;

        /*
         * Even though pFrontBufPriv->canFlip and pBackBufPriv->canFlip is 1, pEvent->type can have DRI2_BLIT.
         * When it requests EXYNOSDri2ScheduleSwapWithRegion(), _canFlip(pDraw) is FALSE. So it has DRI2_BLIT type.
         * In this case we should change pEvent->type to DRI2_FLIP. So we can call exynosCrtcRelFlipPixmap() for pEvent->pBackBuf
         */
        if ((pFrontBufPriv->canFlip == 1) && (pBackBufPriv->canFlip == 1)) {
            pEvent->type = DRI2_FLIP;
        }
    }

    if (pEvent->pBackBuf) {
        /* disuse the backbuffer */
        _disuseBackBufPixmap(pEvent->pBackBuf, pEvent);
        EXYNOSDri2DestroyBuffer(pDraw, pEvent->pBackBuf);
    }

    if (pEvent->pFrontBuf) {
        EXYNOSDri2DestroyBuffer(pDraw, pEvent->pFrontBuf);
    }

    if (pEvent->pRegion) {
        RegionDestroy(pEvent->pRegion);
    }

    free(pEvent);
    pEvent = NULL;
}

static void
_asyncSwapBuffers(ClientPtr pClient, DrawablePtr pDraw,
                  DRI2FrameEventPtr pFrameEvent)
{
    XDBG_DEBUG(MDRI2,
               "id:0x%x(%d) Client:%d Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
               (unsigned int) pDraw->id, pDraw->type, pClient->index,
               pFrameEvent->pFrontBuf->attachment, pFrameEvent->pFrontBuf->name,
               pFrameEvent->pFrontBuf->flags, pFrameEvent->pBackBuf->attachment,
               pFrameEvent->pBackBuf->name, pFrameEvent->pBackBuf->flags);

    _swapFrame(pDraw, pFrameEvent);

    switch (pFrameEvent->type) {
    case DRI2_SWAP:
        DRI2SwapComplete(pClient, pDraw, 0, 0, 0,
                         DRI2_EXCHANGE_COMPLETE,
                         pFrameEvent->event_complete, pFrameEvent->event_data);
        break;
    case DRI2_FLIP:
        _exchangeBuffers(pDraw, pFrameEvent->type, pFrameEvent->pFrontBuf,
                         pFrameEvent->pBackBuf);
        DRI2SwapComplete(pClient, pDraw, 0, 0, 0, DRI2_FLIP_COMPLETE,
                         pFrameEvent->event_complete, pFrameEvent->event_data);
        break;
    case DRI2_BLIT:
    case DRI2_FB_BLIT:
        DRI2SwapComplete(pClient, pDraw, 0, 0, 0,
                         DRI2_BLIT_COMPLETE,
                         pFrameEvent->event_complete, pFrameEvent->event_data);
        break;
    default:
        DRI2SwapComplete(pClient, pDraw, 0, 0, 0,
                         0,
                         pFrameEvent->event_complete, pFrameEvent->event_data);
        break;
    }
}

static Bool
_doPageFlip(DrawablePtr pDraw, int crtc_pipe, xf86CrtcPtr pCrtc,
            DRI2FrameEventPtr pEvent)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    DRI2BufferPrivPtr pBackBufPriv = pEvent->pBackBuf->driverPrivate;
    EXYNOSPixmapPriv *pBackExaPixPriv =
        exaGetPixmapDriverPrivate(pBackBufPriv->pPixmap);

    /* Reset buffer position */
    exynosRenderBoSetPos(pBackExaPixPriv->bo, pDraw->x, pDraw->y);

    if (!exynosModePageFlip(pScrn, NULL, pEvent, crtc_pipe, pBackExaPixPriv->bo,
                            pEvent->pRegion, pEvent->client_idx,
                            pEvent->drawable_id, exynosDri2FlipEventHandler,
                            FALSE)) {
        XDBG_WARNING(MDRI2, "fail to exynosModePageFlip\n");
        return FALSE;
    }
    else {
        XDBG_DEBUG(MDRI2,
                   "doPageFlip id:0x%x(%d) Client:%d pipe:%d Front(attach:%d, name:%d, flag:0x%x), "
                   "Back(attach:%d, name:%d, flag:0x%x )\n",
                   (unsigned int) pDraw->id, pDraw->type,
                   pEvent->pClient->index, crtc_pipe,
                   pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
                   pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
                   pEvent->pBackBuf->name, pEvent->pBackBuf->flags);

        _exchangeBuffers(pDraw, pEvent->type, pEvent->pFrontBuf,
                         pEvent->pBackBuf);
    }

    return TRUE;
}

void
exynosDri2LayerFlipEventHandler(unsigned int frame, unsigned int tv_sec,
                                unsigned int tv_usec, void *event_data,
                                Bool flip_failed)
{
    DRI2FrameEventPtr pEvent = NULL;
    DRI2FrameEventPtr pPendingEvent = NULL;
    DRI2BufferPrivPtr pBackBufPriv = NULL;
    DrawablePtr pDraw = NULL;
    ClientPtr pClient = NULL;

    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:LAYER_FLIP_HANDLER");

    pEvent = (DRI2FrameEventPtr) event_data;
    XDBG_RETURN_IF_FAIL(pEvent != NULL);
    pClient = pEvent->pClient;
    pBackBufPriv = pEvent->pBackBuf->driverPrivate;

    if (pEvent->drawable_id)
        dixLookupDrawable(&pDraw, pEvent->drawable_id, serverClient, M_ANY,
                          DixWriteAccess);

    if (!pDraw) {
        XDBG_WARNING(MDRI2,
                     "pDraw is null... Client:%d pipe:%d "
                     "Front(attach:%d, name:%d, flag:0x%x),"
                     " Back(attach:%d, name:%d, flag:0x%x)\n", pClient->index,
                     pBackBufPriv->pipe, pEvent->pFrontBuf->attachment,
                     pEvent->pFrontBuf->name, pEvent->pFrontBuf->flags,
                     pEvent->pBackBuf->attachment, pEvent->pBackBuf->name,
                     pEvent->pBackBuf->flags);

        //delete event and save pending event
        pPendingEvent = pEvent->pPendingEvent;
        _deleteFrame(pDraw, pEvent);
        pBackBufPriv->pFlipEvent = NULL;

        //delete pending event
        if (pPendingEvent) {
            XDBG_TRACE(MDRI2,
                       "FlipEvent(%d,0x%x) (Break pending flip:draw is not found) Client:%d pipe:%d "
                       "Front(attach:%d, name:%d, flag:0x%x),"
                       " Back(attach:%d, name:%d, flag:0x%x)\n", 0, 0,
                       pClient->index, pBackBufPriv->pipe,
                       pPendingEvent->pFrontBuf->attachment,
                       pPendingEvent->pFrontBuf->name,
                       pPendingEvent->pFrontBuf->flags,
                       pPendingEvent->pBackBuf->attachment,
                       pPendingEvent->pBackBuf->name,
                       pPendingEvent->pBackBuf->flags);
            _deleteFrame(pDraw, pPendingEvent);
        }

        TTRACE_GRAPHICS_END();
        return;
    }

    XDBG_TRACE(MDRI2,
               "FlipEvent(%d,0x%x) Client:%d pipe:%d "
               "Front(attach:%d, name:%d, flag:0x%x),"
               " Back(attach:%d, name:%d, flag:0x%x)\n", pDraw->type,
               (unsigned int) pDraw->id, pClient->index, pBackBufPriv->pipe,
               pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
               pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
               pEvent->pBackBuf->name, pEvent->pBackBuf->flags);

    if (pBackBufPriv->num_buf == 1) {
        DRI2SwapComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec,
                         DRI2_FLIP_COMPLETE, pEvent->event_complete,
                         pEvent->event_data);
    }

    if (pBackBufPriv->pFlipEvent == pEvent) {
        pPendingEvent = pEvent->pPendingEvent;
        _deleteFrame(pDraw, pEvent);
        pBackBufPriv->pFlipEvent = NULL;

        if (pPendingEvent) {
            _scheduleLayerFlip(pDraw, pPendingEvent);
        }
    }
    else {
        XDBG_NEVER_GET_HERE(MDRI2);
    }

    TTRACE_GRAPHICS_END();

}

static void
_bo_swap(tbm_bo front_bo, tbm_bo back_bo)
{
    if (tbm_bo_swap(front_bo, back_bo)) {
        EXYNOSFbBoDataPtr back_bo_data = NULL;
        EXYNOSFbBoDataPtr bo_data = NULL;
        EXYNOSFbBoDataRec tmp_bo_data;

        tbm_bo_get_user_data(front_bo, TBM_BO_DATA_FB, (void * *) &bo_data);
        tbm_bo_get_user_data(back_bo, TBM_BO_DATA_FB, (void * *) &back_bo_data);
        if (back_bo_data && bo_data) {
            memcpy(&tmp_bo_data, bo_data, sizeof(EXYNOSFbBoDataRec));
            memcpy(bo_data, back_bo_data, sizeof(EXYNOSFbBoDataRec));
            memcpy(back_bo_data, &tmp_bo_data, sizeof(EXYNOSFbBoDataRec));
            XDBG_DEBUG(MDRI2,
                       "swap complete: front(bo:%p name:%d(%d) fb:%d) <-> back(bo:%p name:%d(%d) fb:%d)\n",
                       front_bo, tbm_bo_export(front_bo), bo_data->gem_handle,
                       bo_data->fb_id, back_bo, tbm_bo_export(back_bo),
                       back_bo_data->gem_handle, back_bo_data->fb_id);
        }
        else {
            XDBG_DEBUG(MDRI2,
                       "swap complete: front(bo:%p name:%d) <-> back(bo:%p name:%d)\n",
                       front_bo, tbm_bo_export(front_bo), back_bo,
                       tbm_bo_export(back_bo));
            XDBG_WARNING(MDRI2, "user data is NULL: front(%p) back(%p)\n",
                         bo_data, back_bo_data);
        }
    }
    else {
        XDBG_ERROR(MDRI2, "swap fails\n");
    }
}

static Bool
_scheduleLayerFlip(DrawablePtr pDraw, DRI2FrameEventPtr pEvent)
{

    EXYNOSVideoBuf *vbuf = NULL;

    /* TODO: the Draw can be in several layers and
     * we should update frame buffer for each of them
     */
    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, FALSE);
    DRI2BufferPrivPtr pBackBufPriv = pEvent->pBackBuf->driverPrivate;
    EXYNOSPtr pExynos = EXYNOSPTR(xf86Screens[pDraw->pScreen->myNum]);

    XDBG_RETURN_VAL_IF_FAIL(pExynos != NULL, FALSE;
        )
        if (pExynos->isLcdOff) {
        XDBG_WARNING(MDRI2,
                     "LCD OFF : Request a pageflip pending even if the lcd is off.\n");

        _exchangeBuffers(pDraw, DRI2_FLIP, pEvent->pFrontBuf, pEvent->pBackBuf);

        DRI2SwapComplete(pEvent->pClient, pDraw,
                         0, 0, 0,
                         0, pEvent->event_complete, pEvent->event_data);

        pBackBufPriv->pFlipEvent = NULL;
        _deleteFrame(pDraw, pEvent);
        return TRUE;
    }
#ifdef LAYER_MANAGER
    EXYNOSLayerPos lpos = exynosHwcGetDrawLpos(pDraw);

    if (pExynos->hwc_use_def_layer) {
        EXYNOSLayerPos p_lpos[5];
        int max_lpos =
            exynosLayerMngGetListOfOwnedPos(lyr_client_id, 0, p_lpos);

        lpos = p_lpos[max_lpos - 1];
    }
    if (lpos == LAYER_NONE) {
        XDBG_WARNING(MDRI2, "Drawable(0x%x) was deleted from hwc\n",
                     (unsigned int) pDraw->id);
        goto fail;
    }

    if (pBackBufPriv->pFlipEvent) {
        if (pBackBufPriv->pFlipEvent->pPendingEvent) {
            XDBG_ERROR(MDRI2, "Pending event is exist\n");
            goto fail;
        }
        if (pBackBufPriv->num_buf < 2) {
            XDBG_ERROR(MDRI2, "Pending event is exist\n");
            goto fail;
        }

        pBackBufPriv->pFlipEvent->pPendingEvent = pEvent;
        XDBG_TRACE(MDRI2, "FrameEvent(%d,0x%x) set to pending SwapType:%d "
                   "Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
                   pDraw->type, (unsigned int) pDraw->id, pEvent->type,
                   pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
                   pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
                   pEvent->pBackBuf->name, pEvent->pBackBuf->flags);
        return TRUE;
    }

    vbuf =
        exynosUtilCreateVideoBufferByDraw((DrawablePtr) pBackBufPriv->pPixmap);
    XDBG_GOTO_IF_FAIL(vbuf != NULL, fail);
    vbuf->vblank_handler = exynosDri2LayerFlipEventHandler;
    vbuf->vblank_user_data = pEvent;
    exynosHwcSetDriFlag(pDraw, TRUE);
    XDBG_GOTO_IF_FAIL(exynosLayerMngSet
                      (lyr_client_id, 0, 0, NULL, NULL, NULL, vbuf, 0, lpos,
                       NULL, NULL), fail);
#else
    EXYNOSLayerPtr pLayer = NULL;

    pLayer = exynosLayerFindByDraw(pDraw);
    XDBG_GOTO_IF_FAIL(pLayer != NULL, fail);
    if (pLayer == NULL) {
        XDBG_WARNING(MDRI2, "Drawable(0x%x) was deleted from hwc\n",
                     (unsigned int) pDraw->id);
        goto fail;
    }

    if (pBackBufPriv->pFlipEvent) {
        if (pBackBufPriv->pFlipEvent->pPendingEvent) {
            XDBG_ERROR(MDRI2, "Pending event is exist\n");
            goto fail;
        }
        if (pBackBufPriv->num_buf == 1) {
            XDBG_ERROR(MDRI2, "Pending event is exist\n");
            goto fail;
        }
        pBackBufPriv->pFlipEvent->pPendingEvent = pEvent;
        XDBG_TRACE(MDRI2, "FrameEvent(%d,0x%x) set to pending SwapType:%d "
                   "Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
                   pDraw->type, (unsigned int) pDraw->id, pEvent->type,
                   pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
                   pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
                   pEvent->pBackBuf->name, pEvent->pBackBuf->flags);
        return TRUE;
    }
    vbuf =
        exynosUtilCreateVideoBufferByDraw((DrawablePtr) pBackBufPriv->pPixmap);
    XDBG_GOTO_IF_FAIL(vbuf != NULL, fail);

    exynosLayerFreezeUpdate(pLayer, FALSE);
    exynosLayerEnableVBlank(pLayer, TRUE);
    exynosLayerUpdateDRI(pLayer, TRUE);
    vbuf->vblank_handler = exynosDri2LayerFlipEventHandler;
    vbuf->vblank_user_data = pEvent;
    XDBG_GOTO_IF_FAIL(exynosLayerSetBuffer(pLayer, vbuf), fail);
#endif

    exynosUtilVideoBufferUnref(vbuf);   //do unref for vbuf because vbuf is local variable
    _exchangeBuffers(pDraw, DRI2_FLIP, pEvent->pFrontBuf, pEvent->pBackBuf);
    pBackBufPriv->pFlipEvent = pEvent;

    if (pBackBufPriv->num_buf > 1) {
        DRI2SwapComplete(pEvent->pClient, pDraw, 0, 0, 0, 0,
                         pEvent->event_complete, pEvent->event_data);
    }
    return TRUE;
 fail:
    if (vbuf)
        exynosUtilFreeVideoBuffer(vbuf);
    XDBG_WARNING(MDRI2, "fail to FLIP. Do blit\n");
    _blitBuffers(pDraw, pEvent->pFrontBuf, pEvent->pBackBuf);
    DRI2SwapComplete(pEvent->pClient, pDraw,
                     0, 0, 0, 0, pEvent->event_complete, pEvent->event_data);

    XDBG_TRACE(MDRI2,
               "FrameEvent(%d,0x%x) SwapType:%d Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
               pDraw->type, (unsigned int) pDraw->id, pEvent->type,
               pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
               pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
               pEvent->pBackBuf->name, pEvent->pBackBuf->flags);

    _deleteFrame(pDraw, pEvent);
    return FALSE;
}

static Bool
_scheduleFlip(DrawablePtr pDraw, DRI2FrameEventPtr pEvent, Bool bFlipChain)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    xf86CrtcPtr pCrtc = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    BoxRec box;

    /* main crtc for this drawable shall finally deliver pageflip event */
    int crtc_pipe = exynosDisplayDrawablePipe(pDraw);

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;
    pCrtc = exynosModeCoveringCrtc(pScrn, &box, NULL, NULL);

    if (!pCrtc) {
        XDBG_WARNING(MDRI2, "fail to get a crtc from a drawable\n");
        DRI2SwapComplete(pEvent->pClient, pDraw, 0, 0, 0, DRI2_FLIP_COMPLETE,
                         pEvent->event_complete, pEvent->event_data);
        _deleteFrame(pDraw, pEvent);
        return FALSE;
    }

    pEvent->pCrtc = (void *) pCrtc;
    pEvent->crtc_pipe = crtc_pipe;

    pCrtcPriv = pCrtc->driver_private;

    DRI2BufferPrivPtr pBackBufPriv = pEvent->pBackBuf->driverPrivate;

    if (exynosCrtcIsFlipping(pCrtc) || pBackBufPriv->pFlipEvent) {
        /* Set the pending filp frame_event to the back buffer
         * if the previous flip frmae_event is not completed.
         */
        if (pBackBufPriv->pFlipEvent) {
            if (pBackBufPriv->pFlipEvent->pPendingEvent) {
                XDBG_WARNING(MDRI2, "waring : pPendingEvent exist.\n");
                return FALSE;
            }
            pBackBufPriv->pFlipEvent->pPendingEvent = pEvent;
        }

        if (pCrtcPriv->is_fb_blit_flipping || !bFlipChain) {
            exynosCrtcAddPendingFlip(pCrtc, pEvent);
            return TRUE;
        }
    }

    if (!_doPageFlip(pDraw, crtc_pipe, pCrtc, pEvent)) {
        XDBG_WARNING(MDRI2, "_doPageflip failed\n");
        _deleteFrame(pDraw, pEvent);
    }
    else {
        /* set the flip frame_event */
        pBackBufPriv->pFlipEvent = pEvent;
        DRI2SwapComplete(pEvent->pClient, pDraw, 0, 0, 0, DRI2_FLIP_COMPLETE,
                         pEvent->event_complete, pEvent->event_data);
    }

    return TRUE;
}

static void
_saveDrawable(ClientPtr pClient, DrawablePtr pDraw, DRI2BufferPtr pBackBuf,
              DRI2FrameEventType swap_type)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    char *type[5] = { "none", "swap", "flip", "blit", "fbblit" };
    char file[128];
    PixmapPtr pPix;
    DRI2BufferPrivPtr pBackBufPriv;
    EXYNOSPixmapPriv *pExaPixPriv;
    char *appName = NULL;
    const char *cmdName = NULL;

    if (!pExynos->dump_info)
        return;

    XDBG_RETURN_IF_FAIL(pDraw != NULL);
    XDBG_RETURN_IF_FAIL(pBackBuf != NULL);

    pPix = _getPixmapFromDrawable(pDraw);
    XDBG_RETURN_IF_FAIL(pPix != NULL);
    pBackBufPriv = pBackBuf->driverPrivate;
    XDBG_RETURN_IF_FAIL(pBackBufPriv != NULL);
    pExaPixPriv = exaGetPixmapDriverPrivate(pPix);
    XDBG_RETURN_IF_FAIL(pExaPixPriv != NULL);

    cmdName = GetClientCmdName(pClient);

    if (cmdName)
        appName = strrchr(cmdName, '/');

    snprintf(file, sizeof(file), "%03d_%s_%s_%x_%03d.%s",
             pExynos->flip_cnt, type[swap_type],
             (appName ? (++appName) : ("none")), (unsigned int) pDraw->id,
             pExaPixPriv->dump_cnt, pExynos->dump_type);

    if (!strcmp(pExynos->dump_type, DUMP_TYPE_RAW)) {
        Bool need_finish = FALSE;
        EXYNOSPixmapPriv *privPixmap =
            exaGetPixmapDriverPrivate(pBackBufPriv->pPixmap);
        int size;

        if (!privPixmap->bo) {
            need_finish = TRUE;
            exynosExaPrepareAccess(pBackBufPriv->pPixmap, EXA_PREPARE_DEST);
            XDBG_RETURN_IF_FAIL(privPixmap->bo != NULL);
        }
        size = tbm_bo_size(privPixmap->bo);
        exynosUtilDoDumpRaws(pExynos->dump_info, &privPixmap->bo, &size, 1,
                             file);

        if (need_finish)
            exynosExaFinishAccess(pBackBufPriv->pPixmap, EXA_PREPARE_DEST);
    }
    else
        exynosUtilDoDumpPixmaps(pExynos->dump_info, pBackBufPriv->pPixmap, file,
                                pExynos->dump_type);

    XDBG_DEBUG(MSEC, "dump done\n");

    pExaPixPriv->dump_cnt++;
}

static DRI2BufferPtr
EXYNOSDri2CreateBuffer(DrawablePtr pDraw, unsigned int attachment,
                       unsigned int format)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:CREATE_BUF");

    ScreenPtr pScreen = pDraw->pScreen;
    DRI2BufferPtr pBuf = NULL;
    DRI2BufferPrivPtr pBufPriv = NULL;
    PixmapPtr pPix = NULL;
    Bool canFlip = FALSE;

    /* create dri2 buffer */
    pBuf = calloc(1, sizeof(DRI2BufferRec));
    if (pBuf == NULL)
        goto fail;

    /* create dri2 buffer private */
    pBufPriv = calloc(1, sizeof(DRI2BufferPrivRec));
    if (pBufPriv == NULL)
        goto fail;

    /* check canFlip */
    canFlip = _canFlip(pDraw);

    pBuf->driverPrivate = pBufPriv;
    pBuf->format = format;
    pBuf->flags = _getBufferFlag(pDraw, canFlip);

    /* check the attachments */
    if (attachment == DRI2BufferFrontLeft) {
        pPix = _getPixmapFromDrawable(pDraw);
        pPix->refcnt++;
        pBufPriv->canFlip = canFlip;
    }
    else {
        switch (attachment) {
        case DRI2BufferDepth:
        case DRI2BufferDepthStencil:
        case DRI2BufferFakeFrontLeft:
        case DRI2BufferFakeFrontRight:
        case DRI2BufferBackRight:
        case DRI2BufferBackLeft:
            pPix = _initBackBufPixmap(pBuf, pDraw, canFlip);
            if (pPix == NULL) {
                goto fail;
            }
            break;
        default:
            XDBG_ERROR(MDRI2, "Unsupported attachmemt:%d\n", attachment);
            goto fail;
            break;
        }

        //Set DRI2 property for selective-composite mode
        _setDri2Property(pDraw);
    }

    pBuf->cpp = pPix->drawable.bitsPerPixel / 8;
    pBuf->attachment = attachment;
    pBuf->pitch = pPix->devKind;
    pBuf->name = _getName(pPix);
    if (pBuf->name == 0) {
        goto fail;
    }

    pBufPriv->refcnt = 1;
    pBufPriv->attachment = attachment;
    pBufPriv->pPixmap = pPix;
    pBufPriv->pScreen = pScreen;

    XDBG_DEBUG(MDRI2,
               "id:0x%lx(%d) attach:%d, name:%d, flags:0x%x, flip:%d geo(%dx%d+%d+%d)\n",
               pDraw->id, pDraw->type, pBuf->attachment, pBuf->name,
               pBuf->flags, pBufPriv->canFlip, pDraw->width, pDraw->height,
               pDraw->x, pDraw->y);

    TTRACE_GRAPHICS_END();
    return pBuf;
 fail:
    XDBG_WARNING(MDRI2, "Failed: id:0x%lx(%d) attach:%d,geo(%dx%d+%d+%d)\n",
                 pDraw->id, pDraw->type, attachment, pDraw->width,
                 pDraw->height, pDraw->x, pDraw->y);
    if (pPix) {
#if USE_XDBG
        xDbgLogPListDrawRemoveRefPixmap(pDraw, pPix);
#endif
        (*pScreen->DestroyPixmap) (pPix);
    }
    if (pBufPriv)
        free(pBufPriv);
    if (pBuf)
        free(pBuf);

    TTRACE_GRAPHICS_END();

    return NULL;
}

static void
EXYNOSDri2DestroyBuffer(DrawablePtr pDraw, DRI2BufferPtr pBuf)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:DESTROY_BUF");

    ScreenPtr pScreen = NULL;
    DRI2BufferPrivPtr pBufPriv = NULL;

    if (pBuf == NULL) {
        TTRACE_GRAPHICS_END();
        return;
    }

    pBufPriv = pBuf->driverPrivate;
    pScreen = pBufPriv->pScreen;

    _unreferenceBufferPriv(pBuf);

    if (pBufPriv->refcnt == 0) {
        XDBG_DEBUG(MDRI2, "DestroyBuffer(%d:0x%x) name:%d flip:%d\n",
                   pDraw ? pDraw->type : 0,
                   pDraw ? (unsigned int) pDraw->id : 0,
                   pBuf->name, pBufPriv->canFlip);

        if (pBuf->attachment == DRI2BufferFrontLeft) {
            (*pScreen->DestroyPixmap) (pBufPriv->pPixmap);
        }
        else {
            _deinitBackBufPixmap(pBuf, pDraw, pBufPriv->canFlip);
        }

        pBufPriv->pPixmap = NULL;
        free(pBufPriv);
        free(pBuf);
    }

    TTRACE_GRAPHICS_END();
}

static void
EXYNOSDri2CopyRegion(DrawablePtr pDraw, RegionPtr pRegion,
                     DRI2BufferPtr pDstBuf, DRI2BufferPtr pSrcBuf)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:COPY_REGION");

    DRI2BufferPrivPtr pSrcBufPriv = pSrcBuf->driverPrivate;
    DRI2BufferPrivPtr pDstBufPriv = pDstBuf->driverPrivate;
    ScreenPtr pScreen = pDraw->pScreen;
    RegionPtr pCopyClip;
    GCPtr pGc;

    DrawablePtr pSrcDraw = (pSrcBufPriv->attachment == DRI2BufferFrontLeft)
        ? pDraw : &pSrcBufPriv->pPixmap->drawable;
    DrawablePtr pDstDraw = (pDstBufPriv->attachment == DRI2BufferFrontLeft)
        ? pDraw : &pDstBufPriv->pPixmap->drawable;

    pGc = GetScratchGC(pDstDraw->depth, pScreen);
    if (!pGc) {
        TTRACE_GRAPHICS_END();
        return;
    }

    XDBG_DEBUG(MDRI2,
               "CopyRegion(%d,0x%x) Dst(attach:%d, name:%d, flag:0x%x), Src(attach:%d, name:%d, flag:0x%x)\n",
               pDraw->type, (unsigned int) pDraw->id, pDstBuf->attachment,
               pDstBuf->name, pDstBuf->flags, pSrcBuf->attachment,
               pSrcBuf->name, pSrcBuf->flags);

    pCopyClip = REGION_CREATE(pScreen, NULL, 0);
    REGION_COPY(pScreen, pCopyClip, pRegion);
    (*pGc->funcs->ChangeClip) (pGc, CT_REGION, pCopyClip, 0);
    ValidateGC(pDstDraw, pGc);

    /* Wait for the scanline to be outside the region to be copied */
    /* [TODO] Something Do ??? */

    /* It's important that this copy gets submitted before the
     * direct rendering client submits rendering for the next
     * frame, but we don't actually need to submit right now.  The
     * client will wait for the DRI2CopyRegion reply or the swap
     * buffer event before rendering, and we'll hit the flush
     * callback chain before those messages are sent.  We submit
     * our batch buffers from the flush callback chain so we know
     * that will happen before the client tries to render
     * again. */

    (*pGc->ops->CopyArea) (pSrcDraw, pDstDraw,
                           pGc, 0, 0, pDraw->width, pDraw->height, 0, 0);
    (*pGc->funcs->DestroyClip) (pGc);
    FreeScratchGC(pGc);

    TTRACE_GRAPHICS_END();
}

/*
 * ScheduleSwap is responsible for requesting a DRM vblank event for the
 * appropriate frame.
 *
 * In the case of a blit (e.g. for a windowed swap) or buffer exchange,
 * the vblank requested can simply be the last queued swap frame + the swap
 * interval for the drawable.
 *
 * In the case of a page flip, we request an event for the last queued swap
 * frame + swap interval - 1, since we'll need to queue the flip for the frame
 * immediately following the received event.
 *
 * The client will be blocked if it tries to perform further GL commands
 * after queueing a swap, though in the Intel case after queueing a flip, the
 * client is free to queue more commands; they'll block in the kernel if
 * they access buffers busy with the flip.
 *
 * When the swap is complete, the driver should call into the server so it
 * can send any swap complete events that have been requested.
 */
static int
EXYNOSDri2ScheduleSwapWithRegion(ClientPtr pClient, DrawablePtr pDraw,
                                 DRI2BufferPtr pFrontBuf,
                                 DRI2BufferPtr pBackBuf, CARD64 * target_msc,
                                 CARD64 divisor, CARD64 remainder,
                                 DRI2SwapEventPtr swap_func, void *data,
                                 RegionPtr pRegion)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:SWAP_WITH_REGION");

    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int pipe = 0;               /* default */
    int flip = 0;
    DRI2FrameEventPtr pFrameEvent = NULL;
    DRI2FrameEventType swap_type = DRI2_SWAP;
    CARD64 current_msc;
    CARD64 ust, msc;

    pFrameEvent =
        _newFrame(pClient, pDraw, pFrontBuf, pBackBuf, swap_func, data,
                  pRegion);
    if (!pFrameEvent) {
        DRI2SwapComplete(pClient, pDraw, 0, 0, 0, 0, swap_func, data);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }

    /* Set frame count to back */
    {
        PixmapPtr pPix;
        EXYNOSPixmapPriv *pExaPixPriv = NULL;
        DRI2BufferPrivPtr pBufPriv = pBackBuf->driverPrivate;
        CARD64 sbc;
        unsigned int pending;

        pPix = pBufPriv->pBackPixmaps[pBufPriv->cur_idx];
        pExaPixPriv = exaGetPixmapDriverPrivate(pPix);
        DRI2GetSBC(pDraw, &sbc, &pending);
        pExaPixPriv->owner = pDraw->id;
        pExaPixPriv->sbc = sbc + pending;
    }

    swap_type = pFrameEvent->type;

    XDBG_DEBUG(MSEC, "dump_mode(%x) dump_xid(0x%lx:0x%lx) swap_type(%d)\n",
               pExynos->dump_mode, pExynos->dump_xid, pDraw->id, swap_type);

    if ((pExynos->dump_mode & XBERC_DUMP_MODE_DRAWABLE) &&
        (swap_type != DRI2_NONE && swap_type != DRI2_WAITMSC) &&
        (pExynos->dump_xid == 0 || pExynos->dump_xid == pDraw->id))
        _saveDrawable(pClient, pDraw, pBackBuf, swap_type);

#ifdef NO_CRTC_MODE
    if (pExynos->isCrtcOn == FALSE) {
        XDBG_DEBUG(MDRI2, "Not found active crtc using async swa\n");
        _asyncSwapBuffers(pClient, pDraw, pFrameEvent);
        _deleteFrame(pDraw, pFrameEvent);
        TTRACE_GRAPHICS_END();
        return TRUE;
    }
    else
#endif                          //NO_CRTC_MODE
        /* If lcd is off status, SwapBuffers do not consider the vblank sync.
         * The client that launches after lcd is off wants to render the frame
         * on the fly.
         */
    if (pExynos->isLcdOff == TRUE || pExynos->useAsyncSwap == TRUE) {
        XDBG_DEBUG(MDRI2, "LCD is %s. %s Async\n",
                   pExynos->isLcdOff ? "Disable" : "Enable",
                   pExynos->useAsyncSwap ? "Use" : "Not use");
        _asyncSwapBuffers(pClient, pDraw, pFrameEvent);
        _deleteFrame(pDraw, pFrameEvent);
        TTRACE_GRAPHICS_END();
        return TRUE;
    }

    if (!pExynos->hwc_active) {
        pipe = exynosDisplayDrawablePipe(pDraw);

        /* check if the pipe is -1 */
        if (pipe == -1) {
            /* if swap_type is DRI2_FLIP, fall into the async swap */
            if (swap_type == DRI2_FLIP) {
                XDBG_WARNING(MDRI2, "Warning: flip pipe is -1 \n");
                _asyncSwapBuffers(pClient, pDraw, pFrameEvent);
                _deleteFrame(pDraw, pFrameEvent);
                TTRACE_GRAPHICS_END();
                return TRUE;
            }
        }
    }

    /* Truncate to match kernel interfaces; means occasional overflow
     * misses, but that's generally not a big deal */
    *target_msc &= 0xffffffff;
    divisor &= 0xffffffff;
    remainder &= 0xffffffff;

    /* Get current count */
    if (!exynosDisplayGetCurMSC(pScrn, pipe, &ust, &msc)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to get current_msc\n");
        goto blit_fallback;
    }
    current_msc = msc;

    /* Flips need to be submitted one frame before */
    if (swap_type == DRI2_FLIP) {
        flip = 1;
    }

    /* Correct target_msc by 'flip' if swap_type == DRI2_FLIP.
     * Do it early, so handling of different timing constraints
     * for divisor, remainder and msc vs. target_msc works.
     */
    if (*target_msc > 0)
        *target_msc -= flip;

    /*
     * If divisor is zero, or current_msc is smaller than target_msc
     * we just need to make sure target_msc passes before initiating
     * the swap.
     */
    if (divisor == 0 || current_msc < *target_msc) {
        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This makes swap_interval logic more robust.
         */
        if (current_msc >= *target_msc)
            *target_msc = current_msc;

        if (!exynosDisplayVBlank
            (pScrn, pipe, target_msc, flip, VBLANK_INFO_SWAP, pFrameEvent)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to Vblank\n");
            goto blit_fallback;
        }

        pFrameEvent->frame = (unsigned int) *target_msc;

        XDBG_DEBUG(MDRI2,
                   "id:0x%x(%d) SwapType:%d Client:%d pipe:%d Front(attach:%d, name:%d, flag:0x%x), "
                   "Back(attach:%d, name:%d, flag:0x%x )\n",
                   (unsigned int) pDraw->id, pDraw->type, swap_type,
                   pClient->index, pipe, pFrontBuf->attachment, pFrontBuf->name,
                   pFrontBuf->flags, pBackBuf->attachment, pBackBuf->name,
                   pBackBuf->flags);

        if (pFrameEvent->pRegion) {
            BoxPtr pBox = RegionRects(pFrameEvent->pRegion);
            int nBox = RegionNumRects(pFrameEvent->pRegion);

            while (nBox--) {
                XDBG_DEBUG(MDRI2,
                           "Region[%d]: (x1, y1, x2, y2) = (%d,%d,%d,%d) \n ",
                           nBox, pBox->x1, pBox->y1, pBox->x2, pBox->y2);
                pBox++;
            }
        }

        _swapFrame(pDraw, pFrameEvent);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * and we need to queue an event that will satisfy the divisor/remainder
     * equation.
     */
    *target_msc = current_msc - (current_msc % divisor) + remainder;

    /*
     * If the calculated deadline vbl.request.sequence is smaller than
     * or equal to current_msc, it means we've passed the last point
     * when effective onset frame seq could satisfy
     * seq % divisor == remainder, so we need to wait for the next time
     * this will happen.

     * This comparison takes the 1 frame swap delay in pageflipping mode
     * into account, as well as a potential DRM_VBLANK_NEXTONMISS delay
     * if we are blitting/exchanging instead of flipping.
     */
    if (*target_msc <= current_msc)
        *target_msc += divisor;

    /* Account for 1 frame extra pageflip delay if flip > 0 */
    *target_msc -= flip;

    if (!exynosDisplayVBlank
        (pScrn, pipe, target_msc, flip, VBLANK_INFO_SWAP, pFrameEvent)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to Vblank\n");
        goto blit_fallback;
    }

    pFrameEvent->frame = *target_msc;

    XDBG_DEBUG(MDRI2,
               "ScaduleSwap_ex(%d,0x%x) SwapType:%d Client:%d pipe:%d Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
               pDraw->type, (unsigned int) pDraw->id, swap_type, pClient->index,
               pipe, pFrontBuf->attachment, pFrontBuf->name, pFrontBuf->flags,
               pBackBuf->attachment, pBackBuf->name, pBackBuf->flags);

    _swapFrame(pDraw, pFrameEvent);

    TTRACE_GRAPHICS_END();
    return TRUE;

 blit_fallback:
    XDBG_WARNING(MDRI2,
                 "blit_fallback(%d,0x%x) SwapType:%d Client:%d pipe:%d Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
                 pDraw->type, (unsigned int) pDraw->id, swap_type,
                 pClient->index, pipe, pFrontBuf->attachment, pFrontBuf->name,
                 pFrontBuf->flags, pBackBuf->attachment, pBackBuf->name,
                 pBackBuf->flags);

    _blitBuffers(pDraw, pFrontBuf, pBackBuf);

    DRI2SwapComplete(pClient, pDraw, 0, 0, 0, DRI2_BLIT_COMPLETE, swap_func,
                     data);

    if (pFrameEvent) {
        _deleteFrame(pDraw, pFrameEvent);
    }
    *target_msc = 0;            /* offscreen, so zero out target vblank count */

    TTRACE_GRAPHICS_END();

    return TRUE;
}

/*
 * Get current frame count and frame count timestamp, based on drawable's
 * crtc.
 */
static int
EXYNOSDri2GetMSC(DrawablePtr pDraw, CARD64 * ust, CARD64 * msc)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int pipe;

    pipe = exynosDisplayDrawablePipe(pDraw);

    /* Get current count */
    if (!exynosDisplayGetCurMSC(pScrn, pipe, ust, msc)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to get current_msc\n");
        return FALSE;
    }

    return TRUE;
}

/*
 * Request a DRM event when the requested conditions will be satisfied.
 *
 * We need to handle the event and ask the server to wake up the client when
 * we receive it.
 */
static int
EXYNOSDri2ScheduleWaitMSC(ClientPtr pClient, DrawablePtr pDraw,
                          CARD64 target_msc, CARD64 divisor, CARD64 remainder)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    DRI2FrameEventPtr wait_info = NULL;
    CARD64 current_msc;
    CARD64 ust, msc;

    int pipe = 0;

    /* Truncate to match kernel interfaces; means occasional overflow
     * misses, but that's generally not a big deal */
    target_msc &= 0xffffffff;
    divisor &= 0xffffffff;
    remainder &= 0xffffffff;

    /* Drawable not visible, return immediately */
    pipe = exynosDisplayDrawablePipe(pDraw);
    if (pipe == -1)
        goto out_complete;

    wait_info = calloc(1, sizeof(DRI2FrameEventRec));
    if (!wait_info)
        goto out_complete;

    wait_info->drawable_id = pDraw->id;
    wait_info->pClient = pClient;
    wait_info->type = DRI2_WAITMSC;

    /* Get current count */
    if (!exynosDisplayGetCurMSC(pScrn, pipe, &ust, &msc)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to get current_msc\n");
        goto out_complete;
    }
    current_msc = msc;

    /*
     * If divisor is zero, or current_msc is smaller than target_msc,
     * we just need to make sure target_msc passes  before waking up the
     * client.
     */
    if (divisor == 0 || current_msc < target_msc) {
        /* If target_msc already reached or passed, set it to
         * current_msc to ensure we return a reasonable value back
         * to the caller. This keeps the client from continually
         * sending us MSC targets from the past by forcibly updating
         * their count on this call.
         */
        if (current_msc >= target_msc)
            target_msc = current_msc;

        /* flip is 1 to avoid to set DRM_VBLANK_NEXTONMISS */
        if (!exynosDisplayVBlank
            (pScrn, pipe, &target_msc, 1, VBLANK_INFO_SWAP, wait_info)) {
            xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to Vblank\n");
            goto out_complete;
        }

        wait_info->frame = target_msc - 1;      /* reply qeuenct is +1 in exynosDisplayVBlank */
        DRI2BlockClient(pClient, pDraw);
        return TRUE;
    }

    /*
     * If we get here, target_msc has already passed or we don't have one,
     * so we queue an event that will satisfy the divisor/remainder equation.
     */
    target_msc = current_msc - (current_msc % divisor) + remainder;

    /*
     * If calculated remainder is larger than requested remainder,
     * it means we've passed the last point where
     * seq % divisor == remainder, so we need to wait for the next time
     * that will happen.
     */
    if ((current_msc % divisor) >= remainder)
        target_msc += divisor;

    /* flip is 1 to avoid to set DRM_VBLANK_NEXTONMISS */
    if (!exynosDisplayVBlank
        (pScrn, pipe, &target_msc, 1, VBLANK_INFO_SWAP, wait_info)) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "fail to Vblank\n");
        goto out_complete;
    }

    wait_info->frame = target_msc - 1;  /* reply qeuenct is +1 in exynosDisplayVBlank */
    DRI2BlockClient(pClient, pDraw);

    return TRUE;

 out_complete:
    free(wait_info);
    DRI2WaitMSCComplete(pClient, pDraw, target_msc, 0, 0);
    return TRUE;
}

static int
EXYNOSDri2AuthMagic(int fd, uint32_t magic)
{
    int ret;

    ret = drmAuthMagic(fd, (drm_magic_t) magic);

    XDBG_TRACE(MDRI2, "AuthMagic: %d\n", ret);

    return ret;
}

static void
EXYNOSDri2ReuseBufferNotify(DrawablePtr pDraw, DRI2BufferPtr pBuf)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:REUSE_BUF_NOTIFY");

    DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;

    if (!_resetBufPixmap(pDraw, pBuf)) {
        DRI2BufferFlags *flags = (DRI2BufferFlags *) & pBuf->flags;

        pBuf->name = _getName(pBufPriv->pPixmap);
        flags->flags = _getBufferFlag(pDraw, pBufPriv->canFlip);
        flags->data.is_reused = 1;

        /*Set reuse index */
        if (pBuf->attachment != DRI2BufferFrontLeft) {
            DRI2BufferPrivPtr pBufPriv = pBuf->driverPrivate;
            PixmapPtr pPix;
            EXYNOSPixmapPriv *pExaPixPriv = NULL;
            CARD64 sbc;
            unsigned int pending;

            pPix = pBufPriv->pBackPixmaps[pBufPriv->cur_idx];
            pExaPixPriv = exaGetPixmapDriverPrivate(pPix);

            DRI2GetSBC(pDraw, &sbc, &pending);
            /*Get current count */
            if (pExaPixPriv->owner == pDraw->id) {
                unsigned int idx_reuse = sbc + pending - pExaPixPriv->sbc + 1;

                if (idx_reuse > pBufPriv->num_buf + 1) {
                    flags->data.idx_reuse = 0;
                }
                else {
                    flags->data.idx_reuse = idx_reuse;
                }
            }
            else {
                flags->data.idx_reuse = 0;
            }
        }
        else {
            flags->data.idx_reuse = 0;
        }
    }

    XDBG_DEBUG(MDRI2,
               "id:0x%lx(%d) attach:%d, name:%d, flags:0x%x, flip:%d, geo(%dx%d+%d+%d)\n",
               pDraw->id, pDraw->type, pBuf->attachment, pBuf->name,
               pBuf->flags, pBufPriv->canFlip, pDraw->width, pDraw->height,
               pDraw->x, pDraw->y);

    TTRACE_GRAPHICS_END();
}

void
exynosDri2ProcessPending(xf86CrtcPtr pCrtc,
                         unsigned int frame, unsigned int tv_sec,
                         unsigned int tv_usec)
{
    DRI2BufferPrivPtr pBackBufPriv = NULL;
    DrawablePtr pCrtcPendingDraw = NULL;
    DRI2FrameEventPtr pCrtcPendingFlip = NULL;

    pCrtcPendingFlip = exynosCrtcGetFirstPendingFlip(pCrtc);
    if (pCrtcPendingFlip) {
        pBackBufPriv = pCrtcPendingFlip->pBackBuf->driverPrivate;
        ScreenPtr pScreen = pBackBufPriv->pScreen;
        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
        EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

        exynosCrtcRemovePendingFlip(pCrtc, pCrtcPendingFlip);

        if (pCrtcPendingFlip->drawable_id)
            dixLookupDrawable(&pCrtcPendingDraw, pCrtcPendingFlip->drawable_id,
                              serverClient, M_ANY, DixWriteAccess);
        if (!pCrtcPendingDraw) {
            XDBG_WARNING(MDRI2, "pCrtcPendingDraw is null.\n");
            _deleteFrame(pCrtcPendingDraw, pCrtcPendingFlip);
            return;
        }
        else {
            if (pExynos->isLcdOff) {
                XDBG_WARNING(MDRI2,
                             "LCD OFF : Request a pageflip pending even if the lcd is off.\n");

                _exchangeBuffers(pCrtcPendingDraw, DRI2_FLIP,
                                 pCrtcPendingFlip->pFrontBuf,
                                 pCrtcPendingFlip->pBackBuf);

                DRI2SwapComplete(pCrtcPendingFlip->pClient, pCrtcPendingDraw,
                                 frame, tv_sec, tv_usec,
                                 0, pCrtcPendingFlip->event_complete,
                                 pCrtcPendingFlip->event_data);

                pBackBufPriv = pCrtcPendingFlip->pBackBuf->driverPrivate;
                pBackBufPriv->pFlipEvent = NULL;
                _deleteFrame(pCrtcPendingDraw, pCrtcPendingFlip);
            }
            else {
                if (!_scheduleFlip(pCrtcPendingDraw, pCrtcPendingFlip, TRUE)) {
                    XDBG_WARNING(MDRI2,
                                 "fail to _scheduleFlip in exynosDri2FlipEventHandler\n");
                }
            }
        }
    }

}

void
exynosDri2FlipEventHandler(unsigned int frame, unsigned int tv_sec,
                           unsigned int tv_usec, void *event_data,
                           Bool flip_failed)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:FLIP_HANDLER");

    DRI2FrameEventPtr pEvent = event_data;

    if (event_data == NULL) {
        TTRACE_GRAPHICS_END();
        XDBG_NEVER_GET_HERE(MDRI2);
        return;
    }

    DRI2BufferPrivPtr pBackBufPriv = pEvent->pBackBuf->driverPrivate;
    DrawablePtr pDraw = NULL;
    ClientPtr pClient = pEvent->pClient;
    xf86CrtcPtr pCrtc = (xf86CrtcPtr) pEvent->pCrtc;

    if (pEvent->drawable_id)
        dixLookupDrawable(&pDraw, pEvent->drawable_id, serverClient, M_ANY,
                          DixWriteAccess);
    if (!pDraw) {
        XDBG_WARNING(MDRI2, "pDraw is null... Client:%d pipe:%d "
                     "Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
                     pClient->index, pBackBufPriv->pipe,
                     pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
                     pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
                     pEvent->pBackBuf->name, pEvent->pBackBuf->flags);
        exynosDri2ProcessPending(pCrtc, frame, tv_sec, tv_usec);
        _deleteFrame(pDraw, pEvent);

        TTRACE_GRAPHICS_END();
        return;
    }

    XDBG_TRACE(MDRI2, "FlipEvent(%d,0x%x) Client:%d pipe:%d "
               "Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
               pDraw->type, (unsigned int) pDraw->id, pClient->index,
               pBackBufPriv->pipe, pEvent->pFrontBuf->attachment,
               pEvent->pFrontBuf->name, pEvent->pFrontBuf->flags,
               pEvent->pBackBuf->attachment, pEvent->pBackBuf->name,
               pEvent->pBackBuf->flags);

    /* check the failure of the pageflip */
    if (flip_failed) {
        _exchangeBuffers(pDraw, DRI2_FLIP, pEvent->pFrontBuf, pEvent->pBackBuf);

        DRI2SwapComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec,
                         0, pEvent->event_complete, pEvent->event_data);
        _deleteFrame(pDraw, pEvent);

        TTRACE_GRAPHICS_END();
        return;
    }

    assert(pBackBufPriv->pFlipEvent == pEvent);
    pBackBufPriv->pFlipEvent = NULL;
    _deleteFrame(pDraw, pEvent);

    /* get the next pending flip event */
    exynosDri2ProcessPending(pCrtc, frame, tv_sec, tv_usec);

    TTRACE_GRAPHICS_END();
}

void
exynosDri2FrameEventHandler(unsigned int frame, unsigned int tv_sec,
                            unsigned int tv_usec, void *event_data)
{
    TTRACE_GRAPHICS_BEGIN("XORG:DRI2:FRAME_HANDLER");

    DRI2FrameEventPtr pEvent = event_data;
    DrawablePtr pDraw = NULL;
    ScrnInfoPtr pScrn;
    int status;

    status = dixLookupDrawable(&pDraw, pEvent->drawable_id, serverClient,
                               M_ANY, DixWriteAccess);
    if (status != Success) {
        XDBG_WARNING(MDRI2, "drawable is not found\n");

        _deleteFrame(NULL, pEvent);

        TTRACE_GRAPHICS_END();
        return;
    }

    pScrn = xf86ScreenToScrn(pDraw->pScreen);

    XDBG_RETURN_IF_FAIL(pEvent->pFrontBuf != NULL);
    XDBG_RETURN_IF_FAIL(pEvent->pBackBuf != NULL);

    switch (pEvent->type) {
    case DRI2_FLIP:
        if (EXYNOSPTR(pScrn)->hwc_active) {
            if (!_scheduleLayerFlip(pDraw, pEvent))
                XDBG_WARNING(MDRI2, "set_layer fails.\n");
        }
        else {
            if (!_scheduleFlip(pDraw, pEvent, FALSE))
                XDBG_WARNING(MDRI2, "pageflip fails.\n");
        }
        TTRACE_GRAPHICS_END();
        return;
        break;
    case DRI2_SWAP:
        DRI2SwapComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec,
                         DRI2_EXCHANGE_COMPLETE, pEvent->event_complete,
                         pEvent->event_data);
        break;
    case DRI2_BLIT:
    case DRI2_FB_BLIT:
        DRI2SwapComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec,
                         DRI2_BLIT_COMPLETE, pEvent->event_complete,
                         pEvent->event_data);
        break;
    case DRI2_NONE:
        DRI2SwapComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec,
                         0, pEvent->event_complete, pEvent->event_data);
        break;
    case DRI2_WAITMSC:
        DRI2WaitMSCComplete(pEvent->pClient, pDraw, frame, tv_sec, tv_usec);
        break;
    default:
        /* Unknown type */
        break;
    }

    XDBG_DEBUG(MDRI2,
               "FrameEvent(%d,0x%x) SwapType:%d Front(attach:%d, name:%d, flag:0x%x), Back(attach:%d, name:%d, flag:0x%x)\n",
               pDraw->type, (unsigned int) pDraw->id, pEvent->type,
               pEvent->pFrontBuf->attachment, pEvent->pFrontBuf->name,
               pEvent->pFrontBuf->flags, pEvent->pBackBuf->attachment,
               pEvent->pBackBuf->name, pEvent->pBackBuf->flags);

    _deleteFrame(pDraw, pEvent);
    TTRACE_GRAPHICS_END();
}

Bool
exynosDri2Init(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSExaPrivPtr pExaPriv = EXYNOSEXAPTR(pExynos);
    DRI2InfoRec info;
    int ret;
    const char *driverNames[1];

    info.driverName = "exynos-drm";
    info.deviceName = pExynos->drm_device_name;
    info.version = 106;
    info.fd = pExynos->drm_fd;
    info.CreateBuffer = EXYNOSDri2CreateBuffer;
    info.DestroyBuffer = EXYNOSDri2DestroyBuffer;
    info.CopyRegion = EXYNOSDri2CopyRegion;
    info.ScheduleSwap = NULL;
    info.GetMSC = EXYNOSDri2GetMSC;
    info.ScheduleWaitMSC = EXYNOSDri2ScheduleWaitMSC;
    info.AuthMagic = EXYNOSDri2AuthMagic;
    info.ReuseBufferNotify = EXYNOSDri2ReuseBufferNotify;
    info.SwapLimitValidate = NULL;
    /* added in version 7 */
    info.GetParam = NULL;

    /* added in version 8 */
    /* AuthMagic callback which passes extra context */
    /* If this is NULL the AuthMagic callback is used */
    /* If this is non-NULL the AuthMagic callback is ignored */
    info.AuthMagic2 = NULL;

    /* added in version 9 */
    info.CreateBuffer2 = NULL;
    info.DestroyBuffer2 = NULL;
    info.CopyRegion2 = NULL;

    /* add in for Tizen extension */
    info.ScheduleSwapWithRegion = EXYNOSDri2ScheduleSwapWithRegion;

    info.Wait = NULL;
    info.numDrivers = 1;
    info.driverNames = driverNames;
    driverNames[0] = info.driverName;

    ret = DRI2ScreenInit(pScreen, &info);
    if (ret == FALSE) {
        return FALSE;
    }

    /* set the number of the flip back buffers */
    pExaPriv->flip_backbufs = pExynos->flip_bufs - 1;

    //TODO: Own client_id for layer manager
#ifdef LAYER_MANAGER
    if (lyr_client_id == 0)
        lyr_client_id = exynosLayerMngRegisterClient(pScrn, "HWC", 1);
#endif

    //xDbgLogSetLevel (MDRI2, 0);
    return ret;
}

void
exynosDri2Deinit(ScreenPtr pScreen)
{
    DRI2CloseScreen(pScreen);
}
