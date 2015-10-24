/*
 *
 * xserver-xorg-video-exynos
 *
 * Copyright © 2013 Keith Packard
 * Copyright 2010 - 2014 Samsung Electronics co., Ltd. All Rights Reserved.
 *
 * Contact: Roman Marchenko <r.marchenko@samsung.com>
 * Contact: Roman Peresipkyn<r.peresipkyn@samsung.com>
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/types.h>
#include <sys/ioctl.h>
#include <errno.h>

#include <tbm_bufmgr.h>

#include "xorg-server.h"
#include "xf86.h"
#include "xf86drm.h"

#include "windowstr.h"

#include "present.h"

#include "exynos.h"
#include "exynos_accel.h"
#include "exynos_display.h"
#include "exynos_crtc.h"
#include "exynos_layer_manager.h"

static int EXYNOSPresentGetUstMsc(RRCrtcPtr pRRcrtc, CARD64 * ust,
                                  CARD64 * msc);

static EXYNOSLayerMngClientID lyr_client_id = 0;

/*-------------------------- Private structures -----------------------------*/
typedef struct _presentVblankEvent {

    uint64_t event_id;

    RRCrtcPtr pRRcrtc;          //jast for info

} PresentVblankEventRec, *PresentVblankEventPtr;

/*-------------------------- Private functions -----------------------------*/

static void
_saveDrawable(ScrnInfoPtr pScrn, PixmapPtr pPix, WindowPtr pWin, int type)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    char file[128];
    char *str_type[7] =
        { "none", "swap", "flip", "unflip", "flush", "blit", "fbblit" };
    char *appName = NULL;
    const char *clientName = NULL;
    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPix);

    if (!pExynos->dump_info || !(pExynos->dump_mode & XBERC_DUMP_MODE_PRESENT))
        return;

    XDBG_RETURN_IF_FAIL(pPix != NULL);

    if (pWin)
        clientName = GetClientCmdName(wClient(pWin));

    if (clientName)
        appName = strrchr(clientName, '/');

    snprintf(file, sizeof(file), "[Present]%03d_%s_%s_%x_%x_%p_%03d.%s",
             pExynos->flip_cnt,
             str_type[type],
             (appName ? (++appName) : ("none")),
             (pWin ? (unsigned int) pWin->drawable.id : 0),
             (unsigned int) pPix->drawable.id,
             (void *) pPix, pExaPixPriv->dump_cnt, pExynos->dump_type);

    exynosUtilDoDumpPixmaps(pExynos->dump_info, pPix, file, pExynos->dump_type);

    XDBG_DEBUG(MSEC, "dump done\n");

    pExaPixPriv->dump_cnt++;
}

/*-------------------------- Public functions -----------------------------*/
/*
 * Called when the queued vblank event has occurred
 */
void
exynosPresentVblankHandler(unsigned int frame, unsigned int tv_sec,
                           unsigned int tv_usec, void *event_data)
{
    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:VBLANK_HANDLER");

    uint64_t usec = (uint64_t) tv_sec * 1000000 + tv_usec;

    PresentVblankEventRec *pEvent = event_data;

    XDBG_DEBUG(MDRI3, "event_id %llu ust:%llu msc:%u\n", pEvent->event_id, usec,
               frame);
    present_event_notify(pEvent->event_id, usec, frame);
    free(pEvent);

    TTRACE_GRAPHICS_END();
}

/*
 * Called when the queued vblank is aborted
 */
void
exynosPresentVblankAbort(ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void *data)
{
    PresentVblankEventRec *pEvent = data;

    if (pEvent)
        free(pEvent);
}

/*
 * Once the flip has been completed on all pipes, notify the
 * extension code telling it when that happened
 */

void
exynosPresentFlipEventHandler(unsigned int frame, unsigned int tv_sec,
                              unsigned int tv_usec, void *event_data,
                              Bool flip_failed)
{
    PresentVblankEventRec *pEvent = event_data;
    uint64_t ust = (uint64_t) tv_sec * 1000000 + tv_usec;
    uint64_t msc = (uint64_t) frame;

    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:FLIP_HANDLER");

    if (msc == 0) {
        uint64_t tmp_ust;

        EXYNOSPresentGetUstMsc(pEvent->pRRcrtc, &tmp_ust, &msc);
    }

    XDBG_DEBUG(MDRI3, "event_id %llu ust:%llu msc:%llu(%u)\n", pEvent->event_id,
               ust, msc, frame);
    present_event_notify(pEvent->event_id, ust, msc);
    free(pEvent);

    TTRACE_GRAPHICS_END();
}

/*
 * The flip has been aborted, free the structure
 */
void
exynosPresentFlipAbort(void *pageflip_data)
{
    PresentVblankEventRec *pEvent = pageflip_data;

    free(pEvent);
}

/*-------------------------- Callback functions -----------------------------*/
static RRCrtcPtr
EXYNOSPresentGetCrtc(WindowPtr pWindow)
{
    XDBG_RETURN_VAL_IF_FAIL(pWindow != NULL, NULL);

    ScreenPtr pScreen = pWindow->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    BoxRec box, crtcbox;
    xf86CrtcPtr pCrtc = NULL;
    RRCrtcPtr pRandrCrtc = NULL;

    box.x1 = pWindow->drawable.x;
    box.y1 = pWindow->drawable.y;
    box.x2 = box.x1 + pWindow->drawable.width;
    box.y2 = box.y1 + pWindow->drawable.height;

    pCrtc = exynosModeCoveringCrtc(pScrn, &box, NULL, &crtcbox);

    /* Make sure the CRTC is valid and this is the real front buffer */
    if (pCrtc != NULL && !pCrtc->rotatedData)   //TODO what is pCrtc->rotatedData pointing on?
        pRandrCrtc = pCrtc->randr_crtc;

    XDBG_DEBUG(MDRI3, "%s\n", pRandrCrtc ? "OK" : "ERROR");

    return pRandrCrtc;
}

/*
 * The kernel sometimes reports bogus MSC values, especially when
 * suspending and resuming the machine. Deal with this by tracking an
 * offset to ensure that the MSC seen by applications increases
 * monotonically, and at a reasonable pace.
 */
static int
EXYNOSPresentGetUstMsc(RRCrtcPtr pRRcrtc, CARD64 * ust, CARD64 * msc)
{
    XDBG_RETURN_VAL_IF_FAIL(pRRcrtc != NULL, 0);

    xf86CrtcPtr pCrtc = pRRcrtc->devPrivate;
    ScreenPtr pScreen = pRRcrtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    //Get the current msc/ust value from the kernel
    Bool ret = exynosDisplayGetCurMSC(pScrn, pCrtcPriv->pipe, ust, msc);

    XDBG_DEBUG(MDRI3, "%s: pipe:%d ust:%llu msc:%llu\n",
               (ret ? "OK" : "ERROR"), pCrtcPriv->pipe, *ust, *msc);
    return (int) ret;
}

static int
EXYNOSPresentQueueVblank(RRCrtcPtr pRRcrtc, uint64_t event_id, uint64_t msc)
{
    xf86CrtcPtr pCrtc = pRRcrtc->devPrivate;
    ScreenPtr pScreen = pRRcrtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    int pipe = exynosModeGetCrtcPipe(pCrtc);
    PresentVblankEventPtr pEvent = NULL;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    pEvent = calloc(sizeof(PresentVblankEventRec), 1);
    if (!pEvent) {
        XDBG_ERROR(MDRI3, "fail to Vblank: event_id %llu msc %llu \n", event_id,
                   msc);
        return BadAlloc;
    }
    pEvent->event_id = event_id;
    pEvent->pRRcrtc = pRRcrtc;

    if (pExynos->isCrtcOn == FALSE || pExynos->isLcdOff == TRUE) {
        CARD64 ust, msc;

        XDBG_DEBUG(MDRI3, "LCD is OFF... call present_event_notify.\n");
        EXYNOSPresentGetUstMsc(pRRcrtc, &ust, &msc);
        present_event_notify(pEvent->event_id, ust, msc);
        free(pEvent);
        return Success;
    }

    /* flip is 1 to avoid to set DRM_VBLANK_NEXTONMISS */
    if (!exynosDisplayVBlank(pScrn, pipe, &msc, 0, VBLANK_INFO_PRESENT, pEvent)) {
        XDBG_WARNING(MDRI3, "fail to Vblank: event_id %llu msc %llu \n",
                     event_id, msc);
        exynosPresentVblankAbort(pScrn, pCrtc, pEvent);
        return BadAlloc;
    }

    XDBG_DEBUG(MDRI3, "OK to Vblank event_id:%llu msc:%llu \n", event_id, msc);
    return Success;
}

static void
EXYNOSPresentAbortVblank(RRCrtcPtr pRRcrtc, uint64_t event_id, uint64_t msc)
{
    XDBG_INFO(MDRI3, "isn't implamentation\n");

}

static void
EXYNOSPresentFlush(WindowPtr window)
{
    XDBG_INFO(MDRI3, "isn't implamentation\n");

    ScreenPtr pScreen = window->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PixmapPtr pPixmap = (*pScreen->GetWindowPixmap) (window);
    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPixmap);

    _saveDrawable(pScrn, pPixmap, window, 4);

    XDBG_DEBUG(MDRI3, "doPresentFlush id:0x%x\n"
               "pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n",
               (unsigned int) window->drawable.id,
               pPixmap->drawable.serialNumber, pPixmap, pPixmap->drawable.id,
               (pExaPixPriv->bo ? pExaPixPriv->bo : 0),
               (pExaPixPriv->bo ? tbm_bo_export(pExaPixPriv->bo) : -1));
}

static Bool
EXYNOSPresentCheckFlip(RRCrtcPtr pRRcrtc,
                       WindowPtr pWin, PixmapPtr pPixmap, Bool sync_flip)
{

    ScrnInfoPtr pScrn = xf86ScreenToScrn(pWin->drawable.pScreen);
    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPixmap);

    if (!EXYNOSPTR(pScrn)->use_flip)
        return FALSE;

    if (!pExaPixPriv)
        return FALSE;

    if (EXYNOSPTR(pScrn)->hwc_active && !EXYNOSPTR(pScrn)->hwc_use_def_layer) {
        return FALSE;
    }

    if (pExaPixPriv->isFrameBuffer == FALSE) {
        XDBG_RETURN_VAL_IF_FAIL(exynosSwapToRenderBo
                                (pScrn, pWin->drawable.width,
                                 pWin->drawable.height, pExaPixPriv->bo, FALSE),
                                FALSE);

        pExaPixPriv->isFrameBuffer = TRUE;
        pExaPixPriv->sbc = 0;
        pExaPixPriv->size = pPixmap->drawable.height * pPixmap->devKind;
        pExaPixPriv->pWin = pWin;
    }
    pExaPixPriv->owner = pWin->drawable.id;
    return TRUE;
}

#ifdef PRESENT_WINDOW_FLIP
static Bool
EXYNOSPresentWindowFlip(RRCrtcPtr pRRcrtc, uint64_t event_id,
                        uint64_t target_msc, WindowPtr pWindow,
                        PixmapPtr pPixmap, Bool sync_flip)
{
    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:FLIP");

#ifdef LAYER_MANAGER
    EXYNOSVideoBuf *vbuf = NULL;

    /* TODO - process "sync_flip" flag */

    ScreenPtr pScreen = pRRcrtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PresentVblankEventPtr pEvent = NULL;

    pEvent = calloc(sizeof(PresentVblankEventRec), 1);
    XDBG_GOTO_IF_FAIL(pEvent != NULL, fail);
    pEvent->event_id = event_id;
    pEvent->pRRcrtc = pRRcrtc;

    _saveDrawable(pScrn, pPixmap, pWindow, 2);

    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPixmap);

    XDBG_DEBUG(MDRI3,
               "event:%lld target_msc:%lld Drawable(0x%x) Pixmap:(sn:%ld p:%p ID:0x%x, bo(ptr:%p name:%d))\n",
               event_id, target_msc, (unsigned int) pWindow->drawable.id,
               pPixmap->drawable.serialNumber, pPixmap, pPixmap->drawable.id,
               pExaPixPriv->bo, tbm_bo_export(pExaPixPriv->bo));

    DrawablePtr pDraw = &pWindow->drawable;
    EXYNOSLayerPos lpos = exynosHwcGetDrawLpos(pDraw);

    if (lpos == LAYER_NONE) {
        XDBG_ERROR(MDRI3, "Drawable(0x%x) was deleted from HWC\n",
                   (unsigned int) pDraw->id);
        goto fail;
    }

    vbuf = exynosUtilCreateVideoBufferByDraw((DrawablePtr) pPixmap);
    XDBG_GOTO_IF_FAIL(vbuf != NULL, fail);
    vbuf->vblank_handler = exynosPresentFlipEventHandler;
    vbuf->vblank_user_data = pEvent;
    exynosHwcSetDriFlag(pDraw, TRUE);
    XDBG_GOTO_IF_FAIL(exynosLayerMngSet
                      (lyr_client_id, 0, 0, NULL, NULL, NULL, vbuf, 0, lpos,
                       NULL, NULL), fail);

    TTRACE_GRAPHICS_END();
    return TRUE;

 fail:if (vbuf)
        exynosUtilFreeVideoBuffer(vbuf);
    if (pEvent)
        exynosPresentFlipAbort(pEvent);
    XDBG_WARNING(MDRI3, "fail to flip\n");
    TTRACE_GRAPHICS_END();
    return FALSE;

#else
    XDBG_WARNING(MDRI3, "hwc flip not implemented\n");
    TTRACE_GRAPHICS_END();
    return FALSE;
#endif
}
#endif

static Bool
EXYNOSPresentFlip(RRCrtcPtr pRRcrtc,
                  uint64_t event_id,
                  uint64_t target_msc, PixmapPtr pPixmap, Bool sync_flip)
{
    xf86CrtcPtr pCrtc = pRRcrtc->devPrivate;
    ScreenPtr pScreen = pRRcrtc->pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    int pipe = exynosModeGetCrtcPipe(pCrtc);
    PresentVblankEventPtr pEvent = NULL;
    Bool ret;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    /* TODO - process sync_flip flag
     * if (sync_flip)
     *      -//-
     * else
     *      -//-
     * */

    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:FLIP");

    if (pExynos->isCrtcOn == FALSE || pExynos->isLcdOff == TRUE) {
        XDBG_DEBUG(MDRI3, "LCD is OFF... return false.\n");
        TTRACE_GRAPHICS_END();
        return FALSE;
    }

    pEvent = calloc(sizeof(PresentVblankEventRec), 1);
    if (!pEvent) {
        XDBG_ERROR(MDRI3, "fail to flip\n");
        TTRACE_GRAPHICS_END();
        return BadAlloc;
    }
    pEvent->event_id = event_id;
    pEvent->pRRcrtc = pRRcrtc;

    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPixmap);

    /*FIXME - get client id by draw id */
    unsigned int client_idx = 0;

    _saveDrawable(pScrn, pPixmap, pExaPixPriv->pWin, 2);

    exynosRenderBoSetPos(pExaPixPriv->bo, pExaPixPriv->pWin->drawable.x,
                         pExaPixPriv->pWin->drawable.y);

    ret = exynosModePageFlip(pScrn, NULL, pEvent, pipe, pExaPixPriv->bo,
                             NULL, client_idx, pExaPixPriv->pWin->drawable.id,
                             exynosPresentFlipEventHandler, TRUE);
    if (!ret) {
        exynosPresentFlipAbort(pEvent);
        XDBG_WARNING(MDRI3,
                     "fail to flip, error while exynosModePageFlip call\n");
    }
    else {
        PixmapPtr pRootPix = pScreen->GetWindowPixmap(pScreen->root);
        EXYNOSPixmapPriv *pRootPixPriv = exaGetPixmapDriverPrivate(pRootPix);
        PixmapPtr pScreenPix = pScreen->GetScreenPixmap(pScreen);
        EXYNOSPixmapPriv *pScreenPixPriv =
            exaGetPixmapDriverPrivate(pScreenPix);

        XDBG_DEBUG(MDRI3, "doPageFlip id:0x%x Client:%d pipe:%d\n"
                   "Present:pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n"
                   "Root:   pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n"
                   "Screen: pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n",
                   (unsigned int) pExaPixPriv->owner, client_idx, pipe,
                   pPixmap->drawable.serialNumber, pPixmap,
                   pPixmap->drawable.id, pExaPixPriv->bo,
                   tbm_bo_export(pExaPixPriv->bo),
                   pRootPix->drawable.serialNumber, pRootPix,
                   pRootPix->drawable.id, pRootPixPriv->bo,
                   (pRootPixPriv->bo ? tbm_bo_export(pRootPixPriv->bo) : -1),
                   pScreenPix->drawable.serialNumber, pScreenPix,
                   pScreenPix->drawable.id, pScreenPixPriv->bo,
                   (pScreenPixPriv->bo ? tbm_bo_export(pScreenPixPriv->bo) :
                    -1));

    }

    TTRACE_GRAPHICS_END();
    return ret;
}

/*
 * Queue a flip back to the normal frame buffer
 */
static void
EXYNOSPresentUnflip(ScreenPtr pScreen, uint64_t event_id)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PresentVblankEventPtr pEvent = NULL;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    int ret;

    if (!EXYNOSPresentCheckFlip(NULL, pScreen->root, pPixmap, TRUE)) {
        XDBG_WARNING(MDRI3, "fail to check flip for screen pixmap\n");
        return;
    }

    pEvent = calloc(sizeof(PresentVblankEventRec), 1);
    if (!pEvent)
        return;

    pEvent->event_id = event_id;
    pEvent->pRRcrtc = NULL;

    EXYNOSPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate(pPixmap);

    _saveDrawable(pScrn, pPixmap, pExaPixPriv->pWin, 3);

    exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
    XDBG_GOTO_IF_FAIL(pExaPixPriv->bo != NULL, fail);

    ret = exynosModePageFlip(pScrn, NULL, pEvent, -1, pExaPixPriv->bo, NULL, 0,
                             0, exynosPresentFlipEventHandler, TRUE);

    exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

    if (!ret) {
        exynosPresentFlipAbort(pEvent);
        XDBG_WARNING(MDRI3, "fail to flip\n");
    }
    else {
        XDBG_DEBUG(MDRI3, "Unflip doPageFlip id:0x%x Client:%d pipe:%d\n"
                   "Present: screen pix(sn:%ld p:%p ID:0x%x)\n",
                   (unsigned int) pExaPixPriv->owner, 0, -1,
                   pPixmap->drawable.serialNumber, pPixmap,
                   pPixmap->drawable.id);
    }

    return;

 fail:
    if (pEvent)
        free(pEvent);
}

/* The main structure which contains callback functions */
static present_screen_info_rec exynosPresentScreenInfo = {

    .version = PRESENT_SCREEN_INFO_VERSION,

    .get_crtc = EXYNOSPresentGetCrtc,
    .get_ust_msc = EXYNOSPresentGetUstMsc,
    .queue_vblank = EXYNOSPresentQueueVblank,
    .abort_vblank = EXYNOSPresentAbortVblank,
    .flush = EXYNOSPresentFlush,
    .capabilities = PresentCapabilityNone,
    .check_flip = EXYNOSPresentCheckFlip,
    .flip = EXYNOSPresentFlip,
    .unflip = EXYNOSPresentUnflip,
#ifdef PRESENT_WINDOW_FLIP
    /* add in for Tizen extension */
    .window_flip = EXYNOSPresentWindowFlip,
#endif
};

static Bool
_hasAsyncFlip(ScreenPtr pScreen)
{
#ifdef DRM_CAP_ASYNC_PAGE_FLIP
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int ret = 0;
    uint64_t value = 0;

    ret = drmGetCap(pExynos->drm_fd, DRM_CAP_ASYNC_PAGE_FLIP, &value);
    if (ret == 0)
        return value == 1;
#endif
    return FALSE;
}

Bool
exynosPresentScreenInit(ScreenPtr pScreen)
{

    if (_hasAsyncFlip(pScreen))
        exynosPresentScreenInfo.capabilities |= PresentCapabilityAsync;

    lyr_client_id =
        exynosLayerMngRegisterClient(xf86ScreenToScrn(pScreen), "HWC", 1);

    int ret = present_screen_init(pScreen, &exynosPresentScreenInfo);

    if (!ret)
        return FALSE;

    return TRUE;
}
