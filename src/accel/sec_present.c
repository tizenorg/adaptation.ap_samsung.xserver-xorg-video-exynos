/*
 *
 * xserver-xorg-video-sec
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

#include "sec.h"
#include "sec_accel.h"
#include "sec_display.h"
#include "sec_crtc.h"

static int SECPresentGetUstMsc(RRCrtcPtr pRRcrtc, CARD64 *ust, CARD64 *msc);

/*-------------------------- Private structures -----------------------------*/
typedef struct _presentVblankEvent {

	uint64_t event_id;

	RRCrtcPtr pRRcrtc;	//jast for info

} PresentVblankEventRec, *PresentVblankEventPtr;

/*-------------------------- Private functions -----------------------------*/

static void
_saveDrawable (ScrnInfoPtr pScrn, PixmapPtr pPix, WindowPtr pWin, int type)
{
    SECPtr pSec = SECPTR(pScrn);
    char file[128];
    char *str_type[7] = {"none", "swap", "flip", "unflip", "flush", "blit", "fbblit"};
    char *appName = NULL;
    SECPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate (pPix);

    if (!pSec->dump_info)
        return;

    XDBG_RETURN_IF_FAIL (pPix != NULL);

    appName = strrchr( GetClientCmdName(wClient(pWin)), '/');

    snprintf (file, sizeof(file), "[Present]%03d_%s_%s_%x_%x_%p_%03d.%s",
              pSec->flip_cnt,
              str_type[type],
              (appName?(++appName):("none")),
              (unsigned int)pWin->drawable.id,
              (unsigned int)pPix->drawable.id,
              (void *) pPix,
              pExaPixPriv->dump_cnt,
              pSec->dump_type);

    secUtilDoDumpPixmaps (pSec->dump_info, pPix, file, pSec->dump_type);

    XDBG_DEBUG (MSEC, "dump done\n");
}

/*-------------------------- Public functions -----------------------------*/
/*
 * Called when the queued vblank event has occurred
 */
void
secPresentVblankHandler(unsigned int frame, unsigned int tv_sec,
        				unsigned int tv_usec, void *event_data)
{
     TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:VBLANK_HANDLER");

	uint64_t usec = (uint64_t) tv_sec * 1000000 + tv_usec;

	PresentVblankEventRec *pEvent = event_data;

    XDBG_DEBUG(MDRI3, "event_id %lld ust:%lld msc:%lld\n", pEvent->event_id, usec, frame);
	present_event_notify(pEvent->event_id, usec, frame);
    free(pEvent);

    TTRACE_GRAPHICS_END();
}

/*
 * Called when the queued vblank is aborted
 */
void
secPresentVblankAbort(ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void *data)
{
	PresentVblankEventRec *pEvent = data;
    free(pEvent);
}

/*
 * Once the flip has been completed on all pipes, notify the
 * extension code telling it when that happened
 */

void
secPresentFlipEventHandler(unsigned int frame, unsigned int tv_sec,
                         unsigned int tv_usec, void *event_data, Bool flip_failed)
{
    PresentVblankEventRec *pEvent = event_data;
    uint64_t ust = (uint64_t) tv_sec * 1000000 + tv_usec;
    uint64_t msc = (uint64_t)frame;

    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:FLIP_HANDLER");

     if ( msc == 0 )
     {
         uint64_t tmp_ust;
         SECPresentGetUstMsc(pEvent->pRRcrtc, &tmp_ust, &msc);
     }

    XDBG_DEBUG(MDRI3, "event_id %lld ust:%lld msc:%lld(%d)\n", pEvent->event_id, ust, msc, frame);
    present_event_notify(pEvent->event_id, ust, msc);
    free(pEvent);

    TTRACE_GRAPHICS_END();
}

/*
 * The flip has been aborted, free the structure
 */
void
secPresentFlipAbort(void *pageflip_data)
{
	PresentVblankEventRec *pEvent = pageflip_data;
	free(pEvent);
}

/*-------------------------- Callback functions -----------------------------*/
static RRCrtcPtr
SECPresentGetCrtc(WindowPtr pWindow)
{
	XDBG_RETURN_VAL_IF_FAIL (pWindow != NULL, NULL);

	ScreenPtr pScreen = pWindow->drawable.pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	BoxRec box, crtcbox;
	xf86CrtcPtr pCrtc = NULL;
	RRCrtcPtr pRandrCrtc = NULL;

	box.x1 = pWindow->drawable.x;
	box.y1 = pWindow->drawable.y;
	box.x2 = box.x1 + pWindow->drawable.width;
	box.y2 = box.y1 + pWindow->drawable.height;

	pCrtc = secModeCoveringCrtc( pScrn, &box, NULL, &crtcbox );

	/* Make sure the CRTC is valid and this is the real front buffer */
	if (pCrtc != NULL && !pCrtc->rotatedData ) //TODO what is pCrtc->rotatedData pointing on?
			pRandrCrtc = pCrtc->randr_crtc;

	XDBG_DEBUG(MDRI3, "%s\n", pRandrCrtc ?  "OK" : "ERROR");

	return pRandrCrtc;
}

/*
 * The kernel sometimes reports bogus MSC values, especially when
 * suspending and resuming the machine. Deal with this by tracking an
 * offset to ensure that the MSC seen by applications increases
 * monotonically, and at a reasonable pace.
 */
static int
SECPresentGetUstMsc(RRCrtcPtr pRRcrtc, CARD64 *ust, CARD64 *msc)
{
	XDBG_RETURN_VAL_IF_FAIL (pRRcrtc != NULL, 0);

	xf86CrtcPtr pCrtc = pRRcrtc->devPrivate;
	ScreenPtr pScreen = pRRcrtc->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);

	SECCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

	//Get the current msc/ust value from the kernel
	Bool ret = secDisplayGetCurMSC(pScrn, pCrtcPriv->pipe, ust, msc);

	XDBG_DEBUG(MDRI3, "%s: pipe:%d ust:%lld msc:%lld\n",
			(ret?"OK":"ERROR"), pCrtcPriv->pipe, *ust, *msc);
	return (int)ret;
}

static int
SECPresentQueueVblank(RRCrtcPtr 		pRRcrtc,
						 uint64_t 		event_id,
						 uint64_t 		msc)
{
						 xf86CrtcPtr	pCrtc 			= pRRcrtc->devPrivate;
						 ScreenPtr 		pScreen 		= pRRcrtc->pScreen;
						 ScrnInfoPtr 	pScrn 			= xf86ScreenToScrn(pScreen);
						 int 			pipe 			= secModeGetCrtcPipe(pCrtc);
						 PresentVblankEventPtr pEvent 	= NULL;

	pEvent = calloc(sizeof(PresentVblankEventRec), 1);
	if (!pEvent) {
		XDBG_ERROR(MDRI3, "fail to Vblank: event_id %lld msc %llu \n", event_id, msc);
		return BadAlloc;
	}
	pEvent->event_id = event_id;
	pEvent->pRRcrtc = pRRcrtc;


	/* flip is 1 to avoid to set DRM_VBLANK_NEXTONMISS */
	if (!secDisplayVBlank(pScrn, pipe, &msc, 0, VBLANK_INFO_PRESENT,  pEvent)) {
		XDBG_WARNING(MDRI3, "fail to Vblank: event_id %lld msc %llu \n", event_id, msc);
		secPresentVblankAbort(pScrn, pCrtc, pEvent);
		return BadAlloc;
	}

	XDBG_DEBUG(MDRI3, "OK to Vblank event_id:%lld msc:%llu \n", event_id, msc);
	return Success;
}

static void
SECPresentAbortVblank(RRCrtcPtr pRRcrtc, uint64_t event_id, uint64_t msc)
{
	XDBG_INFO(MDRI3, "isn't implamentation\n");

}

static void
SECPresentFlush(WindowPtr window)
{
    XDBG_INFO(MDRI3, "isn't implamentation\n");

    ScreenPtr pScreen = window->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    PixmapPtr pPixmap = (*pScreen->GetWindowPixmap) (window);
    SECPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate (pPixmap);

    _saveDrawable (pScrn, pPixmap, window, 4);

    XDBG_DEBUG(MDRI3, "doPresentFlush id:0x%x\n"
                    "pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n",
                    (unsigned int )window->drawable.id,
                    pPixmap->drawable.serialNumber, pPixmap, pPixmap->drawable.id,
                    (pExaPixPriv->bo ? pExaPixPriv->bo : 0), (pExaPixPriv->bo ? tbm_bo_export(pExaPixPriv->bo) : -1));
}

static Bool
SECPresentCheckFlip(RRCrtcPtr 	pRRcrtc,
					WindowPtr 	pWin,
		 	 	 	PixmapPtr 	pPixmap,
		 	 	 	Bool 		sync_flip)
{

    ScrnInfoPtr pScrn = xf86ScreenToScrn(pWin->drawable.pScreen);
	SECPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate (pPixmap);

	if (!SECPTR(pScrn)->use_flip)
	    return FALSE;

	if (!pExaPixPriv)
	    return FALSE;

	if (pExaPixPriv->isFrameBuffer == FALSE)
    {
    	XDBG_RETURN_VAL_IF_FAIL (secSwapToRenderBo(pScrn, pWin->drawable.width, pWin->drawable.height, pExaPixPriv->bo, FALSE), FALSE);

    	pExaPixPriv->owner = pWin->drawable.id;
        pExaPixPriv->isFrameBuffer = TRUE;
        pExaPixPriv->sbc = 0;
        pExaPixPriv->size = pPixmap->drawable.height * pPixmap->devKind;
        pExaPixPriv->pWin = pWin;
    }
	return TRUE;
}

static Bool
SECPresentFlip(RRCrtcPtr		pRRcrtc,
                   uint64_t		event_id,
                   uint64_t		target_msc,
                   PixmapPtr	pPixmap,
                   Bool			sync_flip)
{
	xf86CrtcPtr pCrtc = pRRcrtc->devPrivate;
	ScreenPtr pScreen = pRRcrtc->pScreen;
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
	int pipe = secModeGetCrtcPipe(pCrtc);
	PresentVblankEventPtr pEvent = NULL;

	Bool ret;

	/* TODO - process sync_flip flag
     * if (sync_flip)
     *      -//-
     * else
     *      -//-
     * */

    TTRACE_GRAPHICS_BEGIN("XORG:PRESENT:FLIP");

	pEvent = calloc(sizeof(PresentVblankEventRec), 1);
	if (!pEvent) {
		XDBG_ERROR(MDRI3, "fail to flip\n");
		return BadAlloc;
	}
	pEvent->event_id = event_id;
	pEvent->pRRcrtc = pRRcrtc;

    SECPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate (pPixmap);

	/*FIXME - get client id by draw id*/
	unsigned int client_idx = 0;

	_saveDrawable (pScrn, pPixmap, pExaPixPriv->pWin, 2);

	ret = secModePageFlip (pScrn, NULL, pEvent, pipe, pExaPixPriv->bo,
						   NULL, client_idx, pExaPixPriv->pWin->drawable.id,
						   secPresentFlipEventHandler, TRUE);
    if (!ret)
    {
        secPresentFlipAbort(pEvent);
        XDBG_WARNING(MDRI3, "fail to flip\n");
    }
    else
    {
        PixmapPtr pRootPix = pScreen->GetWindowPixmap (pScreen->root);
        SECPixmapPriv *pRootPixPriv = exaGetPixmapDriverPrivate (pRootPix);
        PixmapPtr pScreenPix = pScreen->GetScreenPixmap (pScreen);
        SECPixmapPriv *pScreenPixPriv = exaGetPixmapDriverPrivate (pScreenPix);

        XDBG_DEBUG(MDRI3, "doPageFlip id:0x%x Client:%d pipe:%d\n"
                "Present:pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n"
                "Root:   pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n"
                "Screen: pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n",
                (unsigned int )pExaPixPriv->owner, client_idx, pipe,
                pPixmap->drawable.serialNumber, pPixmap, pPixmap->drawable.id,
                pExaPixPriv->bo, tbm_bo_export(pExaPixPriv->bo),
                pRootPix->drawable.serialNumber, pRootPix, pRootPix->drawable.id,
                pRootPixPriv->bo, (pRootPixPriv->bo ? tbm_bo_export(pRootPixPriv->bo) : -1),
                pScreenPix->drawable.serialNumber, pScreenPix, pScreenPix->drawable.id,
                pScreenPixPriv->bo, (pScreenPixPriv->bo ? tbm_bo_export(pScreenPixPriv->bo) : -1));

    }

    TTRACE_GRAPHICS_END();
    return ret;
}

/*
 * Queue a flip back to the normal frame buffer
 */
static void SECPresentUnflip(ScreenPtr pScreen, uint64_t event_id)
{
    ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);
    SECPtr pSec = SECPTR(pScrn);
    PresentVblankEventPtr pEvent = NULL;
    PixmapPtr pPixmap = pScreen->GetScreenPixmap(pScreen);
    tbm_bo *bos = NULL;
    int num_bo;
    int ret;
    tbm_bo_handle src_handle, dst_handle;

    if (!SECPresentCheckFlip(NULL, pScreen->root, pPixmap, TRUE))
    {
        XDBG_WARNING(MDRI3, "fail to check flip for screen pixmap\n");
        return;
    }

    ret = secFbFindBo(pSec->pFb, pPixmap->drawable.x, pPixmap->drawable.y,
            pPixmap->drawable.width, pPixmap->drawable.height, &num_bo, &bos);

    if (ret != rgnSAME)
    {
        XDBG_WARNING(MDRI3, "fail to find frame buffer bo\n");
        free(bos);
        return;
    }

    pEvent = calloc(sizeof(PresentVblankEventRec), 1);
    if (!pEvent)
    {
        free(bos);
        return;
    }

    pEvent->event_id = event_id;
    pEvent->pRRcrtc = NULL;

    SECPixmapPriv *pExaPixPriv = exaGetPixmapDriverPrivate (pPixmap);

    _saveDrawable (pScrn, pPixmap, pExaPixPriv->pWin, 3);

    secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
    XDBG_RETURN_IF_FAIL (pExaPixPriv->bo != NULL);

    src_handle = tbm_bo_get_handle (pExaPixPriv->bo, TBM_DEVICE_CPU);
    dst_handle = tbm_bo_get_handle (bos[0], TBM_DEVICE_CPU);
    memcpy (dst_handle.ptr, src_handle.ptr, tbm_bo_size(pExaPixPriv->bo));

    secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

    ret = secModePageFlip(pScrn, NULL, pEvent, -1, bos[0], NULL, 0,
                0, secPresentFlipEventHandler, TRUE);

    if (!ret) {
        secPresentFlipAbort(pEvent);
        XDBG_WARNING(MDRI3, "fail to flip\n");
    }
    else {

        PixmapPtr pRootPix = pScreen->GetWindowPixmap (pScreen->root);
        SECPixmapPriv *pRootPixPriv = exaGetPixmapDriverPrivate (pRootPix);

        XDBG_DEBUG(MDRI3, "doPageFlip id:0x%x Client:%d pipe:%d\n"
                "Present: pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n"
                "Root:    pix(sn:%ld p:%p ID:0x%x), bo(ptr:%p name:%d)\n",
                (unsigned int )pExaPixPriv->owner, 0, -1,
                pPixmap->drawable.serialNumber, pPixmap, pPixmap->drawable.id,
                bos[0], tbm_bo_export(bos[0]),
                pRootPix->drawable.serialNumber, pRootPix, pRootPix->drawable.id,
                pRootPixPriv->bo, (pRootPixPriv->bo ? tbm_bo_export(pRootPixPriv->bo) : -1));
    }

    free(bos);
}


/* The main structure which contains callback functions */
static present_screen_info_rec secPresentScreenInfo = {

		.version = PRESENT_SCREEN_INFO_VERSION,

	    .get_crtc = SECPresentGetCrtc,
	    .get_ust_msc = SECPresentGetUstMsc,
	    .queue_vblank = SECPresentQueueVblank,
	    .abort_vblank = SECPresentAbortVblank,
	    .flush = SECPresentFlush,
	    .capabilities = PresentCapabilityNone,
	    .check_flip = SECPresentCheckFlip,
	    .flip = SECPresentFlip,
	    .unflip = SECPresentUnflip,
};

static Bool
_hasAsyncFlip(ScreenPtr pScreen)
{
#ifdef DRM_CAP_ASYNC_PAGE_FLIP
	ScrnInfoPtr pScrn = xf86ScreenToScrn(pScreen);;
	SECPtr 		pExynos = SECPTR (pScrn);
	int         ret = 0;
	uint64_t    value = 0;

	ret = drmGetCap(pExynos->drm_fd, DRM_CAP_ASYNC_PAGE_FLIP, &value);
	if (ret == 0)
		return value == 1;
#endif
	return FALSE;
}

Bool
secPresentScreenInit( ScreenPtr pScreen )
{

	if (_hasAsyncFlip(pScreen))
		secPresentScreenInfo.capabilities |= PresentCapabilityAsync;

	int ret = present_screen_init( pScreen, &secPresentScreenInfo );
	if(!ret)
		return FALSE;

	return TRUE;
}


