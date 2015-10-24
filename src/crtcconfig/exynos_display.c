/*
 * Copyright © 2007 Red Hat, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Dave Airlie <airlied@redhat.com>
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <dirent.h>

#include <X11/extensions/dpmsconst.h>
#include <xorgVersion.h>
#include <X11/Xatom.h>
#include <xf86Crtc.h>
#include <xf86DDC.h>
#include <xf86cmap.h>
#include <exynos.h>

#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_plane.h"
#include "exynos_display.h"
#include "exynos_video_fourcc.h"
#include "exynos_wb.h"
#include "exynos_converter.h"
#include "exynos_util.h"
#include "exynos_xberc.h"
#include <xf86RandR12.h>
#include <exynos/exynos_drm.h>
#include "exynos_dummy.h"

static Bool EXYNOSCrtcConfigResize(ScrnInfoPtr pScrn, int width, int height);
static void EXYNOSModeVblankHandler(int fd, unsigned int frame,
                                    unsigned int tv_sec, unsigned int tv_usec,
                                    void *event);
static void EXYNOSModePageFlipHandler(int fd, unsigned int frame,
                                      unsigned int tv_sec, unsigned int tv_usec,
                                      void *event_data);
static void EXYNOSModeG2dHandler(int fd, unsigned int cmdlist_no,
                                 unsigned int tv_sec, unsigned int tv_usec,
                                 void *event_data);
static void EXYNOSModeIppHandler(int fd, unsigned int prop_id,
                                 unsigned int *buf_idx, unsigned int tv_sec,
                                 unsigned int tv_usec, void *event_data);

static const xf86CrtcConfigFuncsRec exynos_xf86crtc_config_funcs = {
    EXYNOSCrtcConfigResize
};

static void
_exynosDisplaySetDrmEventCtx(EXYNOSModePtr pExynosMode)
{
    pExynosMode->event_context.vblank_handler = EXYNOSModeVblankHandler;
    pExynosMode->event_context.page_flip_handler = EXYNOSModePageFlipHandler;
    pExynosMode->event_context.g2d_handler = EXYNOSModeG2dHandler;
    pExynosMode->event_context.ipp_handler = EXYNOSModeIppHandler;
}

static int
_exynosSetMainMode(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode)
{
    xf86CrtcConfigPtr pXf86CrtcConfig;

    pXf86CrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int i;

    for (i = 0; i < pXf86CrtcConfig->num_output; i++) {
        xf86OutputPtr pOutput = pXf86CrtcConfig->output[i];
        EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

        if (!pOutputPriv)
            continue;
        if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS
            || pOutputPriv->mode_output->connector_type ==
            DRM_MODE_CONNECTOR_Unknown) {
            memcpy(&pExynosMode->main_lcd_mode, pOutputPriv->mode_output->modes,
                   sizeof(drmModeModeInfo));
            return 1;
        }
    }
    if (pExynosMode->main_lcd_mode.hdisplay == 0 ||
        pExynosMode->main_lcd_mode.vdisplay == 0) {
        pExynosMode->main_lcd_mode.hdisplay = 640;
        pExynosMode->main_lcd_mode.vdisplay = 480;
        return 1;
    }
    return -1;
}

static void
_exynosDisplayRemoveFlipPixmaps(ScrnInfoPtr pScrn)
{
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int c;

    for (c = 0; c < pCrtcConfig->num_crtc; c++) {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
        int conn_type = exynosCrtcGetConnectType(pCrtc);

        if (conn_type == DRM_MODE_CONNECTOR_Unknown)
            exynosCrtcRemoveFlipPixmap(pCrtc);
    }
}

static void
_saveFrameBuffer(ScrnInfoPtr pScrn, tbm_bo bo, int w, int h)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    char file[128];
    EXYNOSFbBoDataPtr bo_data;

    if (!pExynos->dump_info)
        return;

    tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
    XDBG_RETURN_IF_FAIL(bo_data != NULL);

    snprintf(file, sizeof(file), "%03d_fb_%" PRIdPTR ".bmp", pExynos->flip_cnt,
             bo_data->fb_id);
    exynosUtilDoDumpBmps(pExynos->dump_info, bo, w, h, NULL, file,
                         DUMP_TYPE_BMP);
}

static int
exynosHandleEvent(int fd, exynosDrmEventContextPtr evctx)
{
#define MAX_BUF_SIZE    1024

    char buffer[MAX_BUF_SIZE];
    unsigned int len, i;
    struct drm_event *e;

    /* The DRM read semantics guarantees that we always get only
     * complete events. */
    len = read(fd, buffer, sizeof buffer);
    if (len == 0) {
        XDBG_WARNING(MDISP, "warning: the size of the drm_event is 0.\n");
        return 0;
    }
    if (len < sizeof *e) {
        XDBG_WARNING(MDISP,
                     "warning: the size of the drm_event is less than drm_event structure.\n");
        return -1;
    }
    if (len > MAX_BUF_SIZE - sizeof(struct drm_exynos_ipp_event)) {
        XDBG_WARNING(MDISP,
                     "warning: the size of the drm_event can be over the maximum size.\n");
        return -1;
    }

    i = 0;
    while (i < len && i < MAX_BUF_SIZE) {
        e = (struct drm_event *) &buffer[i];
        switch (e->type) {
        case DRM_EVENT_VBLANK:
        {
            struct drm_event_vblank *vblank;

            if (evctx->vblank_handler == NULL)
                break;

            vblank = (struct drm_event_vblank *) e;
            evctx->vblank_handler(fd,
                                  vblank->sequence,
                                  vblank->tv_sec,
                                  vblank->tv_usec,
                                  (void *) ((unsigned long) vblank->user_data));
        }
            break;
        case DRM_EVENT_FLIP_COMPLETE:
        {
            struct drm_event_vblank *vblank;

            if (evctx->page_flip_handler == NULL)
                break;

            vblank = (struct drm_event_vblank *) e;
            evctx->page_flip_handler(fd,
                                     vblank->sequence,
                                     vblank->tv_sec,
                                     vblank->tv_usec, (void *) ((unsigned long)
                                                                vblank->
                                                                user_data));
        }
            break;
        case DRM_EXYNOS_G2D_EVENT:
        {
            struct drm_exynos_g2d_event *g2d;

            if (evctx->g2d_handler == NULL)
                break;

            g2d = (struct drm_exynos_g2d_event *) e;
            evctx->g2d_handler(fd,
                               g2d->cmdlist_no,
                               g2d->tv_sec,
                               g2d->tv_usec,
                               (void *) ((unsigned long) g2d->user_data));
        }
            break;
        case DRM_EXYNOS_IPP_EVENT:
        {
            struct drm_exynos_ipp_event *ipp;

            if (evctx->ipp_handler == NULL)
                break;

            ipp = (struct drm_exynos_ipp_event *) e;
            evctx->ipp_handler(fd,
                               ipp->prop_id,
                               ipp->buf_id,
                               ipp->tv_sec,
                               ipp->tv_usec,
                               (void *) ((unsigned long) ipp->user_data));
        }
            break;
        default:
            break;
        }
        i += e->length;
    }

    return 0;
}

static Bool
EXYNOSCrtcConfigResize(ScrnInfoPtr pScrn, int width, int height)
{
    ScreenPtr pScreen = pScrn->pScreen;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    XDBG_DEBUG(MDISP, "Resize cur(%dx%d) new(%d,%d)\n",
               pScrn->virtualX, pScrn->virtualY, width, height);
#ifdef NO_CRTC_MODE
    pExynos->isCrtcOn = exynosCrtcCheckInUseAll(pScrn);
#endif
    if (pScrn->virtualX == width && pScrn->virtualY == height) {
        return TRUE;
    }
    exynosFbResize(pExynos->pFb, width, height);

    /* set  the new size of pScrn */
    pScrn->virtualX = width;
    pScrn->virtualY = height;
    pScrn->displayWidth = width;
    exynosExaScreenSetScrnPixmap(pScreen);

    exynosOutputDrmUpdate(pScrn);

    _exynosDisplayRemoveFlipPixmaps(pScrn);

    return TRUE;
}

static void
EXYNOSModeVblankHandler(int fd, unsigned int frame, unsigned int tv_sec,
                        unsigned int tv_usec, void *event)
{
    EXYNOSVBlankInfoPtr pVblankInfo = event;
    EXYNOSVBlankInfoType vblank_type;
    void *data;

    XDBG_RETURN_IF_FAIL(pVblankInfo != NULL);

    vblank_type = pVblankInfo->type;
    data = pVblankInfo->data;

#if DBG_DRM_EVENT
    xDbgLogDrmEventRemoveVblank(pVblankInfo->xdbg_log_vblank);
#endif

    if (vblank_type == VBLANK_INFO_SWAP) {
        XDBG_TRACE(MDISP, "vblank handler (%p, %ld, %ld)\n",
                   pVblankInfo, pVblankInfo->time,
                   GetTimeInMillis() - pVblankInfo->time);
        exynosDri2FrameEventHandler(frame, tv_sec, tv_usec, data);
    }
    else if (vblank_type == VBLANK_INFO_PLANE)
        exynosLayerVBlankEventHandler(frame, tv_sec, tv_usec, data);
    else if (vblank_type == VBLANK_INFO_PRESENT) {
        XDBG_TRACE(MDISP, "vblank handler (%p, %ld, %ld)\n",
                   pVblankInfo, pVblankInfo->time,
                   GetTimeInMillis() - pVblankInfo->time);
        exynosPresentVblankHandler(frame, tv_sec, tv_usec, data);
    }
    else
        XDBG_ERROR(MDISP, "unknown the vblank type\n");

    free(pVblankInfo);
    pVblankInfo = NULL;
}

static void
EXYNOSModePageFlipHandler(int fd, unsigned int frame, unsigned int tv_sec,
                          unsigned int tv_usec, void *event_data)
{
    EXYNOSPageFlipPtr flip = event_data;
    xf86CrtcPtr pCrtc;
    EXYNOSCrtcPrivPtr pCrtcPriv;

    if (!flip) {
        XDBG_ERROR(MDISP, "flip is null\n");
        return;
    }

    XDBG_TRACE(MDISP, "pageflip handler (%p, %ld, %ld)\n",
               flip, flip->time, GetTimeInMillis() - flip->time);

#if DBG_DRM_EVENT
    if (flip->xdbg_log_pageflip != NULL)
        xDbgLogDrmEventRemovePageflip(flip->xdbg_log_pageflip);
#endif

    pCrtc = flip->pCrtc;
    pCrtcPriv = pCrtc->driver_private;
    pCrtcPriv->is_flipping = FALSE;
    pCrtcPriv->is_fb_blit_flipping = FALSE;
    pCrtcPriv->flip_count--;    /* check flipping completed */

    /* Is this the event whose info shall be delivered to higher level? */
    /* Yes: Cache msc, ust for later delivery. */
    pCrtcPriv->fe_frame = frame;
    pCrtcPriv->fe_tv_sec = tv_sec;
    pCrtcPriv->fe_tv_usec = tv_usec;

    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0)
        if (pCrtcPriv->accessibility_front_bo &&
            pCrtcPriv->accessibility_back_bo) {
            tbm_bo temp;

            temp = pCrtcPriv->accessibility_front_bo;
            pCrtcPriv->accessibility_front_bo =
                pCrtcPriv->accessibility_back_bo;
            pCrtcPriv->accessibility_back_bo = temp;
        }

    /* accessibility */
    if (flip->accessibility_back_bo) {
        exynosRenderBoUnref(flip->accessibility_back_bo);
        flip->accessibility_back_bo = NULL;
    }

    /* if accessibility is diabled, remove the accessibility_bo
       when the pageflip is occurred once after the accessibility is disabled */
    if (!pCrtcPriv->bAccessibility && pCrtcPriv->screen_rotate_degree == 0) {
        if (pCrtcPriv->accessibility_front_bo) {
            exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
            pCrtcPriv->accessibility_front_bo = NULL;
        }
        if (pCrtcPriv->accessibility_back_bo) {
            exynosRenderBoUnref(pCrtcPriv->accessibility_back_bo);
            pCrtcPriv->accessibility_back_bo = NULL;
        }
    }

    /* Release back framebuffer */
    if (flip->back_bo) {
        exynosRenderBoUnref(flip->back_bo);
        flip->back_bo = NULL;
    }

    XDBG_DEBUG(MDISP,
               "ModePageFlipHandler ctrc_id:%d dispatch_me:%d, frame:%d, flip_count=%d is_pending=%p\n",
               exynosCrtcID(pCrtcPriv), flip->dispatch_me, frame,
               pCrtcPriv->flip_count, exynosCrtcGetFirstPendingFlip(pCrtc));

    /* Last crtc completed flip? */
    if (flip->dispatch_me) {
        exynosCrtcCountFps(pCrtc);

        /* Deliver cached msc, ust from reference crtc to flip event handler */
        if (flip->handler) {
            if (pCrtcPriv->flip_info != flip->data) {
                XDBG_NEVER_GET_HERE(MDISP);
                pCrtcPriv->flip_info = flip->data;
            }
            flip->handler(pCrtcPriv->fe_frame, pCrtcPriv->fe_tv_sec,
                          pCrtcPriv->fe_tv_usec, pCrtcPriv->flip_info,
                          flip->flip_failed);
        }
    }

    if (pCrtcPriv->flip_info == NULL) {
        /**
         *  If pCrtcPriv->flip_info is failed and exynosCrtcGetFirstPendingFlip (pCrtc) has data,
         *  ModePageFlipHandler is triggered by exynosDisplayUpdateRequest(). - Maybe FB_BLIT or FB is updated by CPU.
         *  In this case we should call exynosDri2ProcessPending().
         */
        XDBG_DEBUG(MDISP,
                   "FB_BLIT or FB is updated by CPU. But there's exynosCrtcGetFirstPendingFlip(). So called it manually\n");
        exynosDri2ProcessPending(pCrtc, frame, tv_sec, tv_usec);
    }

    free(flip);
}

static void
EXYNOSModeG2dHandler(int fd, unsigned int cmdlist_no, unsigned int tv_sec,
                     unsigned int tv_usec, void *event_data)
{
}

static void
EXYNOSModeIppHandler(int fd, unsigned int prop_id, unsigned int *buf_idx,
                     unsigned int tv_sec, unsigned int tv_usec,
                     void *event_data)
{
    XDBG_DEBUG(MDRM, "wb_prop_id(%d) prop_id(%d), buf_idx(%d, %d) \n",
               exynosWbGetPropID(), prop_id, buf_idx[0], buf_idx[1]);

    if (exynosWbGetPropID() == prop_id)
        exynosWbHandleIppEvent(fd, buf_idx, event_data);
    else
        exynosCvtHandleIppEvent(fd, buf_idx, event_data, FALSE);
}

static void
EXYNOSModeWakeupHanlder(pointer data, int err, pointer p)
{
    EXYNOSModePtr pExynosMode;
    fd_set *read_mask;

    if (data == NULL || err < 0)
        return;

    pExynosMode = data;
    read_mask = p;
    if (FD_ISSET(pExynosMode->fd, read_mask))
        exynosHandleEvent(pExynosMode->fd, &pExynosMode->event_context);
}

Bool
exynosModePreInit(ScrnInfoPtr pScrn, int drm_fd)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode;
    unsigned int i;
    int cpp;

//    exynosLogSetLevel(MDISP, 0);

    pExynosMode = calloc(1, sizeof *pExynosMode);
    if (!pExynosMode)
        return FALSE;

    pExynosMode->fd = drm_fd;
    xorg_list_init(&pExynosMode->crtcs);
    xorg_list_init(&pExynosMode->outputs);
    xorg_list_init(&pExynosMode->planes);

    xf86CrtcConfigInit(pScrn, &exynos_xf86crtc_config_funcs);

    cpp = pScrn->bitsPerPixel / 8;

    pExynosMode->cpp = cpp;
    pExynosMode->mode_res = drmModeGetResources(pExynosMode->fd);
    if (!pExynosMode->mode_res) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "failed to get resources: %s\n", strerror(errno));
        free(pExynosMode);
        return FALSE;
    }

    pExynosMode->plane_res = drmModeGetPlaneResources(pExynosMode->fd);
    if (!pExynosMode->plane_res) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "failed to get plane resources: %s\n", strerror(errno));
        drmModeFreeResources(pExynosMode->mode_res);
        free(pExynosMode);
        return FALSE;
    }

    xf86CrtcSetSizeRange(pScrn, 320, 200, pExynosMode->mode_res->max_width,
                         pExynosMode->mode_res->max_height);
#if 1
    for (i = 0; i < pExynosMode->mode_res->count_crtcs; i++)
        exynosCrtcInit(pScrn, pExynosMode, i);

    for (i = 0; i < pExynosMode->mode_res->count_connectors; i++)
        exynosOutputInit(pScrn, pExynosMode, i);
#endif
    for (i = 0; i < pExynosMode->plane_res->count_planes; i++)
        exynosPlaneInit(pScrn, pExynosMode, i);
#if 0
    exynosDummyOutputInit(pScrn, pExynosMode, FALSE);
#endif                          //NO_CRTC_MODE
    _exynosSetMainMode(pScrn, pExynosMode);

    /* virtaul x and virtual y of the screen is ones from main lcd mode */
    pScrn->virtualX = pExynosMode->main_lcd_mode.hdisplay;
    pScrn->virtualY = pExynosMode->main_lcd_mode.vdisplay;
    xf86InitialConfiguration(pScrn, TRUE);

    /* soolim::
     * we assume that kernel always support the pageflipping
     * and the drm vblank
     */
    /* set the drm event context */
    _exynosDisplaySetDrmEventCtx(pExynosMode);

    pExynos->pExynosMode = pExynosMode;

    /* virtaul x and virtual y of the screen is ones from main lcd mode */
    pScrn->virtualX = pExynosMode->main_lcd_mode.hdisplay;
    pScrn->virtualY = pExynosMode->main_lcd_mode.vdisplay;
#ifdef USE_XDBG
#if DBG_DRM_EVENT
    xDbgLogDrmEventInit();
#endif
#endif
    return TRUE;
}

void
exynosModeInit(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    /* We need to re-register the mode->fd for the synchronisation
     * feedback on every server generation, so perform the
     * registration within ScreenInit and not PreInit.
     */
    //pExynosMode->flip_count = 0;
    AddGeneralSocket(pExynosMode->fd);
    RegisterBlockAndWakeupHandlers((BlockHandlerProcPtr) NoopDDA,
                                   EXYNOSModeWakeupHanlder, pExynosMode);

}

void
exynosModeDeinit(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) pExynos->pExynosMode;
    xf86CrtcPtr pCrtc = NULL;
    xf86OutputPtr pOutput = NULL;

    exynosDisplayDeinitDispMode(pScrn);

    EXYNOSCrtcPrivPtr crtc_ref = NULL, crtc_next = NULL;

    xorg_list_for_each_entry_safe(crtc_ref, crtc_next, &pExynosMode->crtcs,
                                  link) {
        pCrtc = crtc_ref->pCrtc;
        xf86CrtcDestroy(pCrtc);
    }

    EXYNOSOutputPrivPtr output_ref = NULL, output_next = NULL;

    xorg_list_for_each_entry_safe(output_ref, output_next,
                                  &pExynosMode->outputs, link) {
        pOutput = output_ref->pOutput;
        xf86OutputDestroy(pOutput);
    }

    EXYNOSPlanePrivPtr plane_ref = NULL, plane_next = NULL;

    xorg_list_for_each_entry_safe(plane_ref, plane_next, &pExynosMode->planes,
                                  link) {
        exynosPlaneDeinit(pScrn, plane_ref);
    }

    if (pExynosMode->mode_res)
        drmModeFreeResources(pExynosMode->mode_res);

    if (pExynosMode->plane_res)
        drmModeFreePlaneResources(pExynosMode->plane_res);

    /* mode->rotate_fb_id should have been destroyed already */

#ifdef NO_CRTC_MODE
    if (pExynosMode->num_dummy_output > 0) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
        int i;

        for (i = 0; i < xf86_config->num_crtc; i++) {
            xf86CrtcPtr pCrtc = xf86_config->crtc[i];

            xf86CrtcDestroy(pCrtc);
        }

        for (i = 0; i < xf86_config->num_output; i++) {
            xf86OutputPtr pOutput = xf86_config->output[i];

            xf86OutputDestroy(pOutput);
        }
    }
#endif                          //NO_CRTC_MODE

    free(pExynosMode);
    pExynos->pExynosMode = NULL;
}

/*
  * Return the crtc covering 'box'. If two crtcs cover a portion of
  * 'box', then prefer 'desired'. If 'desired' is NULL, then prefer the crtc
  * with greater coverage
  */
xf86CrtcPtr
exynosModeCoveringCrtc(ScrnInfoPtr pScrn, BoxPtr pBox, xf86CrtcPtr pDesiredCrtc,
                       BoxPtr pBoxCrtc)
{
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86CrtcPtr pCrtc, pBestCrtc;
    int coverage, best_coverage;
    int c;
    BoxRec crtc_box, cover_box;

    XDBG_RETURN_VAL_IF_FAIL(pBox != NULL, NULL);

    pBestCrtc = NULL;
    best_coverage = 0;

    if (pBoxCrtc) {
        pBoxCrtc->x1 = 0;
        pBoxCrtc->y1 = 0;
        pBoxCrtc->x2 = 0;
        pBoxCrtc->y2 = 0;
    }

    for (c = 0; c < pCrtcConfig->num_crtc; c++) {
        pCrtc = pCrtcConfig->crtc[c];

        /* If the CRTC is off, treat it as not covering */
        if (!exynosCrtcOn(pCrtc))
            continue;

        crtc_box.x1 = pCrtc->x;
        crtc_box.x2 = pCrtc->x + xf86ModeWidth(&pCrtc->mode, pCrtc->rotation);
        crtc_box.y1 = pCrtc->y;
        crtc_box.y2 = pCrtc->y + xf86ModeHeight(&pCrtc->mode, pCrtc->rotation);

        exynosUtilBoxIntersect(&cover_box, &crtc_box, pBox);
        coverage = exynosUtilBoxArea(&cover_box);

        if (coverage && pCrtc == pDesiredCrtc) {
            if (pBoxCrtc)
                *pBoxCrtc = crtc_box;
            return pCrtc;
        }

        if (coverage > best_coverage) {
            if (pBoxCrtc)
                *pBoxCrtc = crtc_box;
            pBestCrtc = pCrtc;
            best_coverage = coverage;
        }
    }

    return pBestCrtc;
}

int
exynosModeGetCrtcPipe(xf86CrtcPtr pCrtc)
{
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return -1;
    return pCrtcPriv->pipe;
}

Bool
exynosModePageFlip(ScrnInfoPtr pScrn, xf86CrtcPtr pCrtc, void *flip_info,
                   int pipe, tbm_bo back_bo, RegionPtr pFlipRegion,
                   unsigned int client_idx, XID drawable_id,
                   EXYNOSFlipEventHandler handler, Bool change_front)
{
    EXYNOSPageFlipPtr pPageFlip = NULL;
    EXYNOSFbBoDataPtr bo_data;
    EXYNOSCrtcPrivPtr pCrtcPriv;
    EXYNOSModePtr pExynosMode;
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86CrtcPtr pCurCrtc;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int ret;
    int fb_id = 0;

    BoxRec b1;
    int retBox, found = 0;
    int i;

    tbm_bo_get_user_data(back_bo, TBM_BO_DATA_FB, (void * *) &bo_data);
    XDBG_RETURN_VAL_IF_FAIL(bo_data != NULL, FALSE);

    for (i = 0; i < pCrtcConfig->num_crtc; i++) {
        pCurCrtc = pCrtcConfig->crtc[i];
        if (!pCurCrtc->enabled)
            continue;
        pCrtcPriv = pCurCrtc->driver_private;
        pExynosMode = pCrtcPriv->pExynosMode;

        b1.x1 = pCurCrtc->x;
        b1.y1 = pCurCrtc->y;
        b1.x2 = pCurCrtc->x + pCurCrtc->mode.HDisplay;
        b1.y2 = pCurCrtc->y + pCurCrtc->mode.VDisplay;

        retBox = exynosUtilBoxInBox(&bo_data->pos, &b1);
        if (retBox == rgnSAME || retBox == rgnIN) {
            pPageFlip = calloc(1, sizeof(EXYNOSPageFlipRec));
            if (pPageFlip == NULL) {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                           "Page flip alloc failed\n");
                return FALSE;
            }

            /* Only the reference crtc will finally deliver its page flip
             * completion event. All other crtc's events will be discarded.
             */
            pPageFlip->dispatch_me = 0;
            pPageFlip->pCrtc = pCurCrtc;
            pPageFlip->clone = TRUE;
            pPageFlip->back_bo = exynosRenderBoRef(back_bo);
            pPageFlip->data = flip_info;
            pPageFlip->flip_failed = FALSE;
            pPageFlip->handler = handler;

            /* accessilitity */
            if (pCrtcPriv->bAccessibility ||
                pCrtcPriv->screen_rotate_degree > 0) {
                tbm_bo accessibility_bo = pCrtcPriv->accessibility_back_bo;
                EXYNOSFbBoDataPtr accessibility_bo_data;

                tbm_bo_get_user_data(accessibility_bo, TBM_BO_DATA_FB,
                                     (void * *) &accessibility_bo_data);
                XDBG_GOTO_IF_FAIL(accessibility_bo_data != NULL, fail);

                fb_id = accessibility_bo_data->fb_id;

                /*Buffer is already changed by bo_swap */
                if (!exynosCrtcExecAccessibility
                    (pCurCrtc, back_bo, accessibility_bo))
                    goto fail;

                pPageFlip->accessibility_back_bo =
                    exynosRenderBoRef(accessibility_bo);
            }
            else {
                fb_id = bo_data->fb_id;

                tbm_bo_map(pPageFlip->back_bo, TBM_DEVICE_2D, TBM_OPTION_READ);
                tbm_bo_unmap(pPageFlip->back_bo);
            }

            pCrtcPriv->is_flipping = TRUE;

            if (!pCrtcPriv->onoff && !pCrtcPriv->onoff_always)
                exynosCrtcTurn(pCrtcPriv->pCrtc, TRUE, FALSE, FALSE);

#if DBG_DRM_EVENT
            pPageFlip->xdbg_log_pageflip =
                xDbgLogDrmEventAddPageflip(pipe, client_idx, drawable_id);
#endif

            XDBG_DEBUG(MSEC, "dump_mode(%x)\n", pExynos->dump_mode);

            if (pExynos->dump_mode & XBERC_DUMP_MODE_FB)
                _saveFrameBuffer(pScrn, back_bo,
                                 bo_data->pos.x2 - bo_data->pos.x1,
                                 bo_data->pos.y2 - bo_data->pos.y1);

            pPageFlip->time = GetTimeInMillis();

            /*Set DirtyFB */
            if (pExynos->use_partial_update && pFlipRegion) {
                int nBox;
                BoxPtr pBox;
                RegionRec new_region;
                RegionPtr pRegion = pFlipRegion;

                for (nBox = RegionNumRects(pRegion),
                     pBox = RegionRects(pRegion); nBox--; pBox++) {
                    XDBG_DEBUG(MDISP, "dirtfb region(%d): (%d,%d %dx%d)\n",
                               nBox, pBox->x1, pBox->y1, pBox->x2 - pBox->x1,
                               pBox->y2 - pBox->y1);
                }

                if (pCrtcPriv->screen_rotate_degree > 0) {
                    RegionCopy(&new_region, pFlipRegion);
                    exynosUtilRotateRegion(pCrtc->mode.HDisplay,
                                           pCrtc->mode.VDisplay, &new_region,
                                           pCrtcPriv->screen_rotate_degree);
                    pRegion = &new_region;

                    for (nBox = RegionNumRects(pRegion),
                         pBox = RegionRects(pRegion); nBox--; pBox++) {
                        XDBG_DEBUG(MDISP,
                                   "(R)dirtfb region(%d): (%d,%d %dx%d)\n",
                                   nBox, pBox->x1, pBox->y1,
                                   pBox->x2 - pBox->x1, pBox->y2 - pBox->y1);
                    }
                }

                drmModeDirtyFB(pExynos->drm_fd, fb_id,
                               (drmModeClipPtr) RegionRects(pRegion),
                               (uint32_t) RegionNumRects(pRegion));
            }

            /* DRM Page Flip */
            ret =
                drmModePageFlip(pExynos->drm_fd, exynosCrtcID(pCrtcPriv), fb_id,
                                DRM_MODE_PAGE_FLIP_EVENT, pPageFlip);
            pExynos->flip_cnt++;

            if (ret) {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                           "Page flip failed: %s\n", strerror(errno));
                goto fail;
            }

            XDBG_TRACE(MDISP, "pageflip do (%p, %ld)\n", pPageFlip,
                       pPageFlip->time);

            pCrtcPriv->flip_count++;    /* check flipping completed */
            pCrtcPriv->flip_info = (DRI2FrameEventPtr) flip_info;
            if (change_front)
                pCrtcPriv->front_bo = back_bo;

            found++;
            XDBG_DEBUG(MDISP,
                       "ModePageFlip crtc_id:%d, fb_id:%d, back_fb_id:%d, back_name:%d, accessibility:%d\n",
                       exynosCrtcID(pCrtcPriv), fb_id, bo_data->fb_id,
                       tbm_bo_export(back_bo), pCrtcPriv->bAccessibility);
        }
    }

    if (found == 0) {
        XDBG_WARNING(MDISP, "Cannot find CRTC in (%d,%d)-(%d,%d)\n",
                     bo_data->pos.x1, bo_data->pos.y1, bo_data->pos.x2,
                     bo_data->pos.y2);
        return FALSE;
    }

    /* Set dispatch_me to last pageflip */
    pPageFlip->dispatch_me = 1;

    return TRUE;

 fail:
    pCrtcPriv->flip_count++;    /* check flipping completed */
    pCrtcPriv->flip_info = (DRI2FrameEventPtr) flip_info;
    pPageFlip->dispatch_me = 1;
    pPageFlip->flip_failed = TRUE;

    EXYNOSModePageFlipHandler(pExynosMode->fd, 0, 0, 0, pPageFlip);

    XDBG_ERROR(MDISP,
               "drmModePageFlip error(crtc:%d, fb_id:%d, back_fb_id:%d, back_name:%d, accessibility:%d)\n",
               exynosCrtcID(pCrtcPriv), fb_id, bo_data->fb_id,
               tbm_bo_export(back_bo), pCrtcPriv->bAccessibility);

    return TRUE;
}

/* load palette per a crtc */
void
exynosModeLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices,
                      LOCO * colors, VisualPtr pVisual)
{
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int i, j, index;
    int p;
    uint16_t lut_r[256], lut_g[256], lut_b[256];

    for (p = 0; p < pCrtcConfig->num_crtc; p++) {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[p];

        switch (pScrn->depth) {
        case 16:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                if (index <= 31) {
                    for (j = 0; j < 8; j++) {
                        lut_r[index * 8 + j] = colors[index].red << 8;
                        lut_b[index * 8 + j] = colors[index].blue << 8;
                    }
                }
                for (j = 0; j < 4; j++) {
                    lut_g[index * 4 + j] = colors[index].green << 8;
                }
            }
            break;
        default:
            for (i = 0; i < numColors; i++) {
                index = indices[i];
                lut_r[index] = colors[index].red << 8;
                lut_g[index] = colors[index].green << 8;
                lut_b[index] = colors[index].blue << 8;

            }
            break;
        }

        /* make the change through RandR */
        RRCrtcGammaSet(pCrtc->randr_crtc, lut_r, lut_g, lut_b);
    }
}

void
exynosDisplaySwapModeFromKmode(ScrnInfoPtr pScrn,
                               drmModeModeInfoPtr kmode, DisplayModePtr pMode)
{
    char fake_name[32] = "fake_mode";

    memset(pMode, 0, sizeof(DisplayModeRec));
    pMode->status = MODE_OK;

    pMode->Clock = kmode->clock;

    pMode->HDisplay = kmode->vdisplay;
    pMode->HSyncStart = kmode->vsync_start;
    pMode->HSyncEnd = kmode->vsync_end;
    pMode->HTotal = kmode->vtotal;
    pMode->HSkew = kmode->vscan;

    pMode->VDisplay = kmode->hdisplay;
    pMode->VSyncStart = kmode->hsync_start;
    pMode->VSyncEnd = kmode->hsync_end;
    pMode->VTotal = kmode->htotal;
    pMode->VScan = kmode->hskew;

    pMode->Flags = kmode->flags;        //& FLAG_BITS;
    pMode->name = strdup(fake_name);

    if (kmode->type & DRM_MODE_TYPE_DRIVER)
        pMode->type = M_T_DRIVER;
    if (kmode->type & DRM_MODE_TYPE_PREFERRED)
        pMode->type |= M_T_PREFERRED;

    xf86SetModeCrtc(pMode, pScrn->adjustFlags);
}

void
exynosDisplayModeFromKmode(ScrnInfoPtr pScrn,
                           drmModeModeInfoPtr kmode, DisplayModePtr pMode)
{
    memset(pMode, 0, sizeof(DisplayModeRec));
    pMode->status = MODE_OK;

    pMode->Clock = kmode->clock;

    pMode->HDisplay = kmode->hdisplay;
    pMode->HSyncStart = kmode->hsync_start;
    pMode->HSyncEnd = kmode->hsync_end;
    pMode->HTotal = kmode->htotal;
    pMode->HSkew = kmode->hskew;

    pMode->VDisplay = kmode->vdisplay;
    pMode->VSyncStart = kmode->vsync_start;
    pMode->VSyncEnd = kmode->vsync_end;
    pMode->VTotal = kmode->vtotal;
    pMode->VScan = kmode->vscan;

    pMode->Flags = kmode->flags;        //& FLAG_BITS;
    pMode->name = strdup(kmode->name);
    pMode->VRefresh = kmode->vrefresh;

    if (kmode->type & DRM_MODE_TYPE_DRIVER)
        pMode->type = M_T_DRIVER;
    if (kmode->type & DRM_MODE_TYPE_PREFERRED)
        pMode->type |= M_T_PREFERRED;

    xf86SetModeCrtc(pMode, pScrn->adjustFlags);
}

void
exynosDisplaySwapModeToKmode(ScrnInfoPtr pScrn,
                             drmModeModeInfoPtr kmode, DisplayModePtr pMode)
{
    memset(kmode, 0, sizeof(*kmode));

    kmode->clock = pMode->Clock;
    kmode->hdisplay = pMode->VDisplay;
    kmode->hsync_start = pMode->VSyncStart;
    kmode->hsync_end = pMode->VSyncEnd;
    kmode->htotal = pMode->VTotal;
    kmode->hskew = pMode->VScan;

    kmode->vdisplay = pMode->HDisplay;
    kmode->vsync_start = pMode->HSyncStart;
    kmode->vsync_end = pMode->HSyncEnd;
    kmode->vtotal = pMode->HTotal;
    kmode->vscan = pMode->HSkew;
    kmode->vrefresh = xf86ModeVRefresh(pMode);

    kmode->flags = pMode->Flags;        //& FLAG_BITS;
    if (pMode->name)
        strncpy(kmode->name, pMode->name, DRM_DISPLAY_MODE_LEN);
    kmode->name[DRM_DISPLAY_MODE_LEN - 1] = 0;
}

void
exynosDisplayModeToKmode(ScrnInfoPtr pScrn,
                         drmModeModeInfoPtr kmode, DisplayModePtr pMode)
{
    memset(kmode, 0, sizeof(*kmode));

    kmode->clock = pMode->Clock;
    kmode->hdisplay = pMode->HDisplay;
    kmode->hsync_start = pMode->HSyncStart;
    kmode->hsync_end = pMode->HSyncEnd;
    kmode->htotal = pMode->HTotal;
    kmode->hskew = pMode->HSkew;

    kmode->vdisplay = pMode->VDisplay;
    kmode->vsync_start = pMode->VSyncStart;
    kmode->vsync_end = pMode->VSyncEnd;
    kmode->vtotal = pMode->VTotal;
    kmode->vscan = pMode->VScan;
    kmode->vrefresh = xf86ModeVRefresh(pMode);

    kmode->flags = pMode->Flags;        //& FLAG_BITS;
    if (pMode->name)
        strncpy(kmode->name, pMode->name, DRM_DISPLAY_MODE_LEN);

    kmode->name[DRM_DISPLAY_MODE_LEN - 1] = 0;
}

static uint32_t crtc_id;
static tbm_bo hdmi_bo;

static Bool connect_crtc;

static int
_exynosDisplayGetAvailableCrtcID(ScrnInfoPtr pScrn)
{
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    int i;
    int crtc_id = 0;

    for (i = 0; i < pExynosMode->mode_res->count_crtcs; i++) {
        drmModeCrtcPtr kcrtc = NULL;

        kcrtc =
            drmModeGetCrtc(pExynosMode->fd, pExynosMode->mode_res->crtcs[i]);
        if (!kcrtc) {
            XDBG_ERROR(MSEC, "fail to get kcrtc. \n");
            return 0;
        }

        if (kcrtc->buffer_id > 0) {
            drmModeFreeCrtc(kcrtc);
            continue;
        }

        crtc_id = kcrtc->crtc_id;
        drmModeFreeCrtc(kcrtc);

        return crtc_id;
    }

    return 0;
}

static void
_exynosDisplayWbCloseFunc(EXYNOSWb * wb, EXYNOSWbNotify noti, void *noti_data,
                          void *user_data)
{
    ScrnInfoPtr pScrn = (ScrnInfoPtr) user_data;
    EXYNOSPtr pExynos;

    if (!pScrn)
        return;

    pExynos = EXYNOSPTR(pScrn);

    pExynos->wb_clone = NULL;
}

Bool
exynosDisplayInitDispMode(ScrnInfoPtr pScrn, EXYNOSDisplayConnMode conn_mode)
{
    XDBG_DEBUG(MDISP, "E. pScrn %p, conn_mode %d\n", pScrn, conn_mode);
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    Bool ret = FALSE;
    uint32_t *output_ids = NULL;
    int output_cnt = 1;
    uint32_t fb_id;
    drmModeModeInfoPtr pKmode = NULL;
    drmModeCrtcPtr kcrtc = NULL;
    EXYNOSFbBoDataPtr bo_data = NULL;
    EXYNOSOutputPrivPtr pOutputPriv = NULL, pNext = NULL;
    int connector_type = -1;
    int width, height;

    if (connect_crtc) {
        XDBG_DEBUG(MDISP, "Q. Connect_crtc is %s. ret = TRUE\n",
                   connect_crtc ? "TRUE" : "FALSE");
        return TRUE;
    }

    /* get output ids */
    output_ids = calloc(output_cnt, sizeof(uint32_t));
    XDBG_GOTO_IF_FAIL(output_ids != NULL, fail_to_init);

    xorg_list_for_each_entry_safe(pOutputPriv, pNext, &pExynosMode->outputs,
                                  link) {
        if (conn_mode == DISPLAY_CONN_MODE_HDMI) {
            if (pOutputPriv->mode_output->connector_type ==
                DRM_MODE_CONNECTOR_HDMIA ||
                pOutputPriv->mode_output->connector_type ==
                DRM_MODE_CONNECTOR_HDMIB) {
                output_ids[0] = pOutputPriv->mode_output->connector_id;
                pKmode = &pExynosMode->ext_connector_mode;
                connector_type = pOutputPriv->mode_output->connector_type;
                break;
            }
        }
        else if (conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
            if (pOutputPriv->mode_output->connector_type ==
                DRM_MODE_CONNECTOR_VIRTUAL) {
                output_ids[0] = pOutputPriv->mode_output->connector_id;
                pKmode = &pExynosMode->ext_connector_mode;
                connector_type = pOutputPriv->mode_output->connector_type;
                break;
            }

        }
        else {
            XDBG_NEVER_GET_HERE(MTVO);
            goto fail_to_init;
        }
    }
    XDBG_GOTO_IF_FAIL(output_ids[0] > 0, fail_to_init);
    XDBG_GOTO_IF_FAIL(pKmode != NULL, fail_to_init);

    width = pKmode->hdisplay;
    height = pKmode->vdisplay;

    pOutputPriv = exynosOutputGetPrivateForConnType(pScrn, connector_type);
    if (pOutputPriv && pOutputPriv->mode_encoder)
        XDBG_GOTO_IF_FAIL(pOutputPriv->mode_encoder->crtc_id == 0,
                          fail_to_init);

    crtc_id = _exynosDisplayGetAvailableCrtcID(pScrn);
    XDBG_GOTO_IF_FAIL(crtc_id > 0, fail_to_init);

    /* get crtc_id */
    kcrtc = drmModeGetCrtc(pExynosMode->fd, crtc_id);
    XDBG_GOTO_IF_FAIL(kcrtc != NULL, fail_to_init);

    if (kcrtc->buffer_id > 0) {
        XDBG_ERROR(MTVO, "crtc(%d) already has buffer(%d) \n",
                   crtc_id, kcrtc->buffer_id);
        goto fail_to_init;
    }

    /* get fb_id */
    hdmi_bo = exynosRenderBoCreate(pScrn, width, height);
    XDBG_GOTO_IF_FAIL(hdmi_bo != NULL, fail_to_init);

    tbm_bo_get_user_data(hdmi_bo, TBM_BO_DATA_FB, (void * *) &bo_data);
    XDBG_GOTO_IF_FAIL(bo_data != NULL, fail_to_init);

    fb_id = bo_data->fb_id;

    /* set crtc */
    if (drmModeSetCrtc
        (pExynosMode->fd, crtc_id, fb_id, 0, 0, output_ids, output_cnt,
         pKmode)) {
        XDBG_ERRNO(MTVO, "drmModeSetCrtc failed. \n");
        goto fail_to_init;
    }
    else {
        ret = TRUE;
    }

    exynosUtilSetDrmProperty(pExynosMode, crtc_id, DRM_MODE_OBJECT_CRTC, "mode",
                             1);

    exynosOutputDrmUpdate(pScrn);

    XDBG_INFO(MDISP, "** ModeSet : (%dx%d) %dHz !!\n", pKmode->hdisplay,
              pKmode->vdisplay, pKmode->vrefresh);

    connect_crtc = TRUE;

 fail_to_init:
    free(output_ids);
    if (kcrtc)
        drmModeFreeCrtc(kcrtc);
    XDBG_DEBUG(MDISP, "Q. ret = %s\n", ret ? "TRUE" : "FALSE");
    return ret;
}

void
exynosDisplayDeinitDispMode(ScrnInfoPtr pScrn)
{
    XDBG_DEBUG(MDISP, "E. pScrn %p\n", pScrn);
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;

    if (!connect_crtc) {
        XDBG_DEBUG(MDISP, "Q. connect_crtc is %s\n",
                   connect_crtc ? "TRUE" : "FALSE");
        return;
    }

    XDBG_INFO(MDISP, "** ModeUnset. !!\n");

    exynosUtilSetDrmProperty(pExynosMode, crtc_id, DRM_MODE_OBJECT_CRTC, "mode",
                             0);

    if (hdmi_bo) {
        exynosRenderBoUnref(hdmi_bo);
        hdmi_bo = NULL;
    }

    exynosOutputDrmUpdate(pScrn);

    crtc_id = 0;
    connect_crtc = FALSE;
    XDBG_DEBUG(MDISP, "Q.\n");
}

Bool
exynosDisplaySetDispSetMode(ScrnInfoPtr pScrn, EXYNOSDisplaySetMode set_mode)
{
    XDBG_DEBUG(MDISP, "E. pScrn %p set_mode %u\n", pScrn, set_mode);
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    if (pExynosMode->set_mode == set_mode) {
        XDBG_TRACE(MDISP, "set_mode(%u) is already set\n", set_mode);
        XDBG_DEBUG(MDISP, "Q. ret = TRUE\n");
        return TRUE;
    }

    if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_NONE) {
        XDBG_WARNING(MDISP,
                     "set_mode(%d) is failed : output is not connected yet\n",
                     set_mode);
        XDBG_DEBUG(MDISP, "Q. ret = FALSE\n");
        return FALSE;
    }

    switch (set_mode) {
    case DISPLAY_SET_MODE_OFF:
        if (exynosWbIsOpened()) {
            EXYNOSWb *wb = exynosWbGet();

            exynosWbClose(wb);
        }
        exynosDisplayDeinitDispMode(pScrn);
        break;
    case DISPLAY_SET_MODE_CLONE:
        /* In case of DISPLAY_CONN_MODE_VIRTUAL, we will open writeback
         * on GetStill.
         */
        if (pExynosMode->conn_mode != DISPLAY_CONN_MODE_VIRTUAL) {
            int wb_hz;

            if (pExynos->wb_clone) {
                XDBG_ERROR(MWB, "Fail : wb_clone(%p) already exists.\n",
                           pExynos->wb_clone);
                break;
            }

            if (exynosWbIsOpened()) {
                XDBG_ERROR(MWB, "Fail : wb(%p) already opened.\n",
                           exynosWbGet());
                break;
            }

            wb_hz =
                (pExynos->wb_hz >
                 0) ? pExynos->wb_hz : pExynosMode->ext_connector_mode.vrefresh;

            XDBG_TRACE(MWB, "wb_hz(%d) vrefresh(%d)\n", pExynos->wb_hz,
                       pExynosMode->ext_connector_mode.vrefresh);

            pExynos->wb_clone =
                exynosWbOpen(pScrn, FOURCC_RGB32, 0, 0,
                             (pExynos->scanout) ? TRUE : FALSE, wb_hz, TRUE);
            if (pExynos->wb_clone) {
                exynosWbAddNotifyFunc(pExynos->wb_clone, WB_NOTI_CLOSED,
                                      _exynosDisplayWbCloseFunc, pScrn);
                exynosWbSetRotate(pExynos->wb_clone, pExynosMode->rotate);
                exynosWbSetTvout(pExynos->wb_clone, TRUE);
                if (!exynosWbStart(pExynos->wb_clone)) {
                    exynosWbClose(pExynos->wb_clone);
                    XDBG_DEBUG(MDISP, "Q. Can't start WB. ret = FALSE\n");
                    return FALSE;
                }
            }
        }
        break;
    case DISPLAY_SET_MODE_EXT:
        if (exynosWbIsOpened()) {
            EXYNOSWb *wb = exynosWbGet();

            exynosWbClose(wb);
        }
        exynosDisplayDeinitDispMode(pScrn);
        break;
    default:
        break;
    }

    pExynosMode->set_mode = set_mode;
    XDBG_DEBUG(MDISP, "Q. ret = TRUE\n");

    return TRUE;
}

EXYNOSDisplaySetMode
exynosDisplayGetDispSetMode(ScrnInfoPtr pScrn)
{
    EXYNOSDisplaySetMode set_mode;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    set_mode = pExynosMode->set_mode;

    return set_mode;
}

Bool
exynosDisplaySetDispRotate(ScrnInfoPtr pScrn, int rotate)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    if (pExynosMode->rotate == rotate)
        return TRUE;

    pExynosMode->rotate = rotate;

    if (pExynos->wb_clone)
        exynosWbSetRotate(pExynos->wb_clone, rotate);

    return TRUE;
}

int
exynosDisplayGetDispRotate(ScrnInfoPtr pScrn)
{
    int rotate;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    rotate = pExynosMode->rotate;

    return rotate;
}

Bool
exynosDisplaySetDispConnMode(ScrnInfoPtr pScrn, EXYNOSDisplayConnMode conn_mode)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    if (pExynosMode->conn_mode == conn_mode) {
        XDBG_DEBUG(MDISP, "conn_mode(%d) is already set\n", conn_mode);
        return TRUE;
    }

    switch (conn_mode) {
    case DISPLAY_CONN_MODE_NONE:
        break;
    case DISPLAY_CONN_MODE_HDMI:
        break;
    case DISPLAY_CONN_MODE_VIRTUAL:
        break;
    default:
        break;
    }

    pExynosMode->conn_mode = conn_mode;

    return TRUE;
}

EXYNOSDisplayConnMode
exynosDisplayGetDispConnMode(ScrnInfoPtr pScrn)
{
    EXYNOSDisplayConnMode conn_mode;

    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    conn_mode = pExynosMode->conn_mode;

    return conn_mode;
}

Bool
exynosDisplayGetCurMSC(ScrnInfoPtr pScrn, intptr_t pipe, CARD64 * ust,
                       CARD64 * msc)
{
    drmVBlank vbl;
    int ret;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    /* if lcd is off, return true with msc = 0 */
#ifdef NO_CRTC_MODE
    if (pExynos->isCrtcOn == FALSE) {
        *ust = 0;
        *msc = 0;
        return TRUE;
    }
#endif                          //NO_CRTC_MODE
//    if (pExynos->isLcdOff)
//    {
//        *ust = 0;
//        *msc = 0;
//        return TRUE;
//    }

    /* if pipe is -1, return the current msc of the main crtc */
    if (pipe == -1)
        pipe = 0;

    vbl.request.type = DRM_VBLANK_RELATIVE;

    if (pipe > 0) {
#ifdef LEGACY_INTERFACE
        EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            vbl.request.type |= _DRM_VBLANK_EXYNOS_VIDI;
        else
#endif
            vbl.request.type |= DRM_VBLANK_SECONDARY;
    }

    vbl.request.sequence = 0;
    ret = drmWaitVBlank(pExynos->drm_fd, &vbl);
    if (ret) {
        *ust = 0;
        *msc = 0;
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "first get vblank counter failed: %s\n", strerror(errno));
        return FALSE;
    }

    *ust = ((CARD64) vbl.reply.tval_sec * 1000000) + vbl.reply.tval_usec;
    *msc = vbl.reply.sequence;

    return TRUE;
}

Bool
exynosDisplayVBlank(ScrnInfoPtr pScrn, intptr_t pipe, CARD64 * target_msc,
                    intptr_t flip, EXYNOSVBlankInfoType type, void *vblank_info)
{
    drmVBlank vbl;
    int ret;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSVBlankInfoPtr pVblankInfo = NULL;
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    pVblankInfo = calloc(1, sizeof(EXYNOSVBlankInfoRec));
    if (pVblankInfo == NULL) {
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING, "vblank_info alloc failed\n");
        return FALSE;
    }

    pVblankInfo->type = type;
    pVblankInfo->data = vblank_info;
    pVblankInfo->time = GetTimeInMillis();

    vbl.request.type = DRM_VBLANK_ABSOLUTE | DRM_VBLANK_EVENT;

    if (pipe > 0) {
#ifdef LEGACY_INTERFACE
        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            vbl.request.type |= _DRM_VBLANK_EXYNOS_VIDI;
        else
#endif
            vbl.request.type |= DRM_VBLANK_SECONDARY;
    }

    /* If non-pageflipping, but blitting/exchanging, we need to use
     * DRM_VBLANK_NEXTONMISS to avoid unreliable timestamping later
     * on.
     */
    if (flip == 0) {
        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL && pipe > 0);   /* do not set the DRM_VBLANK_NEXTMISS */
        else
            vbl.request.type |= DRM_VBLANK_NEXTONMISS;
    }

    vbl.request.sequence = *target_msc;
    vbl.request.signal = (unsigned long) pVblankInfo;

#if DBG_DRM_EVENT
    DRI2FrameEventPtr pEvent = (DRI2FrameEventPtr) vblank_info;

    if (type == VBLANK_INFO_SWAP)
        pVblankInfo->xdbg_log_vblank =
            xDbgLogDrmEventAddVblank(pipe, pEvent->client_idx,
                                     pEvent->drawable_id, type);
    else
        pVblankInfo->xdbg_log_vblank =
            xDbgLogDrmEventAddVblank(pipe, 0, 0, type);
#endif
    ret = drmWaitVBlank(pExynos->drm_fd, &vbl);
    if (ret) {
#if DBG_DRM_EVENT
        xDbgLogDrmEventRemoveVblank(pVblankInfo->xdbg_log_vblank);
#endif
        if (pVblankInfo) {
            free(pVblankInfo);
            pVblankInfo = NULL;
        }
        xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                   "divisor 0 get vblank counter failed: %s\n",
                   strerror(errno));
        return FALSE;
    }

    XDBG_TRACE(MDISP, "vblank do (%p, %ld)\n", pVblankInfo, pVblankInfo->time);

    /* Adjust returned value for 1 fame pageflip offset of flip > 0 */
    *target_msc = vbl.reply.sequence + flip;

    return TRUE;
}

int
exynosDisplayDrawablePipe(DrawablePtr pDraw)
{
    ScreenPtr pScreen = pDraw->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    BoxRec box, crtc_box;
    xf86CrtcPtr pCrtc;
    int pipe = -1;

    box.x1 = pDraw->x;
    box.y1 = pDraw->y;
    box.x2 = box.x1 + pDraw->width;
    box.y2 = box.y1 + pDraw->height;

    pCrtc = exynosModeCoveringCrtc(pScrn, &box, NULL, &crtc_box);

    if (pCrtc != NULL && !pCrtc->rotatedData)
        pipe = exynosModeGetCrtcPipe(pCrtc);

    return pipe;
}

intptr_t
exynosDisplayCrtcPipe(ScrnInfoPtr pScrn, int crtc_id)
{
    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(pScrn);
    int c;

    for (c = 0; c < pCrtcConfig->num_crtc; c++) {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
        EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

        if (pCrtcPriv->mode_crtc->crtc_id == crtc_id)
            return pCrtcPriv->pipe;
    }

    XDBG_ERROR(MDISP, "%s(%d): crtc(%d) not found.\n", __func__, __LINE__,
               crtc_id);

    for (c = 0; c < pCrtcConfig->num_crtc; c++) {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
        EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

        XDBG_ERROR(MDISP, "%s(%d) : crtc(%d) != crtc(%d)\n", __func__, __LINE__,
                   pCrtcPriv->mode_crtc->crtc_id, crtc_id);
    }

    return 0;
}

Bool
exynosDisplayUpdateRequest(ScrnInfoPtr pScrn)
{
    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, FALSE);

    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    xf86CrtcPtr pCrtc = xf86CompatCrtc(pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv;
    tbm_bo bo;
    Bool ret = FALSE;
    EXYNOSPageFlipPtr pPageFlip = NULL;

#ifdef NO_CRTC_MODE
    if (pCrtc == NULL)
        return FALSE;
#else
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);
#endif
    pCrtcPriv = pCrtc->driver_private;
#ifdef NO_CRTC_MODE
    if (pCrtcPriv == NULL)
        return TRUE;
#else
    XDBG_RETURN_VAL_IF_FAIL(pCrtcPriv != NULL, FALSE);
#endif

    bo = pCrtcPriv->front_bo;

    if ((pExynos->use_hwc && pExynos->hwc_active) || !pExynos->use_flip) {
        XDBG_DEBUG(MDISP, "hwc enable\n");
    }
    else if (pCrtcPriv->is_fb_blit_flipping || pCrtcPriv->is_flipping ||
             exynosCrtcGetFirstPendingFlip(pCrtc)) {
        XDBG_DEBUG(MDISP, "drmModePageFlip is already requested!\n");
    }
    else {
        // Without buffer swap, we need to request drmModePageFlip().
        if (bo != NULL) {
            EXYNOSFbBoDataPtr bo_data;
            int fb_id = 0;

            tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);
            XDBG_RETURN_VAL_IF_FAIL(bo_data != NULL, FALSE);

            fb_id = bo_data->fb_id;

            pPageFlip = calloc(1, sizeof(EXYNOSPageFlipRec));
            if (pPageFlip == NULL) {
                xf86DrvMsg(pScrn->scrnIndex, X_WARNING,
                           "Page flip alloc failed\n");
                return FALSE;
            }

            /* Only the reference crtc will finally deliver its page flip
             * completion event. All other crtc's events will be discarded.
             */
            pPageFlip->dispatch_me = 0;
            pPageFlip->pCrtc = pCrtc;
            pPageFlip->clone = TRUE;
            pPageFlip->back_bo = exynosRenderBoRef(bo);
            pPageFlip->data = NULL;
            pPageFlip->flip_failed = FALSE;
            pPageFlip->xdbg_log_pageflip = NULL;
            pPageFlip->time = GetTimeInMillis();
            /*TODO: add own handler */
            pPageFlip->handler = NULL;

            /* accessilitity */
            if (pCrtcPriv->bAccessibility ||
                pCrtcPriv->screen_rotate_degree > 0) {
                tbm_bo accessibility_bo = pCrtcPriv->accessibility_back_bo;
                EXYNOSFbBoDataPtr accessibility_bo_data;

                tbm_bo_get_user_data(accessibility_bo, TBM_BO_DATA_FB,
                                     (void * *) &accessibility_bo_data);
                XDBG_GOTO_IF_FAIL(accessibility_bo_data != NULL, fail);

                fb_id = accessibility_bo_data->fb_id;

                /*Buffer is already changed by bo_swap */
                if (!exynosCrtcExecAccessibility(pCrtc, bo, accessibility_bo))
                    goto fail;

                pPageFlip->accessibility_back_bo =
                    exynosRenderBoRef(accessibility_bo);
            }

            /*
             * DRM Page Flip
             * If pPageFlip->dispatch_me is NULL, then in EXYNOSModePageFlipHandler, nothing to happen.
             * That means only LCD buffer is updated.
             * Frame buffer is not swapped. Because these request is only for FB_BLIT case!
             */
            if (drmModePageFlip(pExynos->drm_fd, exynosCrtcID(pCrtcPriv), fb_id,
                                DRM_MODE_PAGE_FLIP_EVENT, pPageFlip)) {
                XDBG_ERRNO(MDISP, "Page flip failed: %s\n", strerror(errno));
                goto fail;
            }

            pCrtcPriv->flip_info = NULL;
            pCrtcPriv->flip_count++;
            pCrtcPriv->is_fb_blit_flipping = TRUE;
        }
        else {
            XDBG_DEBUG(MDISP, "pCrtcPriv->front_bo is NULL!\n");
        }
    }

    ret = TRUE;

    return ret;

 fail:

    if (pPageFlip != NULL) {
        if (pPageFlip->accessibility_back_bo) {
            exynosRenderBoUnref(pPageFlip->accessibility_back_bo);
            pPageFlip->accessibility_back_bo = NULL;
        }

        if (pPageFlip->back_bo) {
            exynosRenderBoUnref(pPageFlip->back_bo);
            pPageFlip->back_bo = NULL;
        }

        free(pPageFlip);
    }

    return ret;
}

#ifdef NO_CRTC_MODE
Bool
exynosDisplayChangeMode(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;
    xf86CrtcPtr pCrtc = NULL;
    xf86OutputPtr pOutput = NULL;
    int temp = 99, i = 0;
    xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);

    for (i = 0; i < config->num_crtc; i++) {
        if (config->crtc[i]->active == TRUE) {
            /* Nothing to do */
            return TRUE;
        }
    }
    EXYNOSOutputPrivPtr output_ref = NULL, output_next = NULL;

/* Priority LVDS > HDMI > Virtual */
    xorg_list_for_each_entry_safe(output_ref, output_next,
                                  &pExynosMode->outputs, link) {
        if (output_ref->pOutput->crtc == NULL)
            continue;
        if (output_ref->mode_output == NULL)
            continue;
        if (output_ref->mode_output->connector_type ==
            DRM_MODE_CONNECTOR_Unknown)
            continue;
        if (output_ref->mode_output->connector_type < temp) {
            pOutput = output_ref->pOutput;
            pCrtc = output_ref->pOutput->crtc;
            temp = (int) output_ref->mode_output->connector_type;
        }
    }

    if (pCrtc != NULL && pOutput != NULL && pCrtc->active == FALSE) {
        DisplayModePtr max_mode = xf86CVTMode(4096, 4096, 60, 0, 0);
        DisplayModePtr mode = xf86OutputFindClosestMode(pOutput, max_mode);

        if (mode == NULL) {
            XDBG_ERROR(MDISP,
                       "Can't find display mode for output: %s\n",
                       pOutput->name);
            if (max_mode)
                free(max_mode);
            return FALSE;
        }
        if (!RRScreenSizeSet(pScrn->pScreen,
                             mode->HDisplay, mode->VDisplay,
                             pOutput->mm_width, pOutput->mm_height)) {
            XDBG_ERROR(MDISP,
                       "Can't setup screen size H:%d V:%d for output: %s\n",
                       mode->HDisplay, mode->VDisplay, pOutput->name);
            if (max_mode)
                free(max_mode);
            return FALSE;
        }
        if (!xf86CrtcSetModeTransform(pCrtc, mode, RR_Rotate_0, NULL, 0, 0)) {
            XDBG_ERROR(MDISP, "Can't transform Crtc to mode: %s\n", mode->name);
            if (max_mode)
                free(max_mode);
            RRScreenSizeSet(pScrn->pScreen, 640, 480, 0, 0);
            return FALSE;
        }
        if (max_mode)
            free(max_mode);
        pExynos->enableCursor = TRUE;
        exynosCrtcCursorEnable(pScrn, TRUE);
//        xf86CrtcConfigPtr config = XF86_CRTC_CONFIG_PTR(pScrn);
//        for (temp = 0; temp < config->num_output; temp++)
//        {
//            if (config->output[temp] == pOutput)
//            {
//                config->compat_output = temp;
//                break;
//            }
//        }
        xf86SetScrnInfoModes(pScrn);
        xf86RandR12TellChanged(pScrn->pScreen);
    }

    if (pExynos->isCrtcOn == FALSE) {
        pExynos->enableCursor = FALSE;
        exynosCrtcCursorEnable(pScrn, FALSE);
    }

    if (pCrtc == NULL) {
        RRScreenSizeSet(pScrn->pScreen, 640, 480, 0, 0);
        xf86SetScrnInfoModes(pScrn);
        xf86RandR12TellChanged(pScrn->pScreen);
    }

    return TRUE;
}
#endif
