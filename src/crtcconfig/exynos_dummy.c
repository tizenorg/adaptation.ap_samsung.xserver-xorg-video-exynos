/*
 * Copyright © 2013 Intel Corporation
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
 *	Chris Wilson <chris@chris-wilson.co.uk>
 *
 */
#include "exynos_dummy.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_util.h"
#include "exynos_display.h"
#include "exynos_accel.h"
#ifdef NO_CRTC_MODE
static void
_dummyFlipPixmapInit(xf86CrtcPtr pCrtc)
{
    XDBG_DEBUG(MDOUT, "\n");
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    XDBG_RETURN_IF_FAIL(pCrtc != NULL);

    int flip_backbufs = pExynos->flip_bufs - 1;
    int i;

    pCrtcPriv->flip_backpixs.lub = -1;
    pCrtcPriv->flip_backpixs.num = flip_backbufs;

    pCrtcPriv->flip_backpixs.pix_free = calloc(flip_backbufs, sizeof(void *));
    XDBG_RETURN_IF_FAIL(pCrtcPriv->flip_backpixs.pix_free != NULL);

    for (i = 0; i < flip_backbufs; i++)
        pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
    pCrtcPriv->flip_backpixs.flip_pixmaps =
        calloc(flip_backbufs, sizeof(void *));
    pCrtcPriv->flip_backpixs.flip_draws = calloc(flip_backbufs, sizeof(void *));
}

static void
_dummyFlipPixmapDeinit(xf86CrtcPtr pCrtc)
{
    XDBG_DEBUG(MDOUT, "\n");
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL)
        return;
    ScreenPtr pScreen = pCrtc->scrn->pScreen;
    int i;

    for (i = 0; i < pCrtcPriv->flip_backpixs.num; i++) {
        pCrtcPriv->flip_backpixs.pix_free[i] = TRUE;
        if (pCrtcPriv->flip_backpixs.flip_pixmaps[i]) {
#if USE_XDBG
            if (pCrtcPriv->flip_backpixs.flip_draws[i])
                xDbgLogPListDrawRemoveRefPixmap(pCrtcPriv->flip_backpixs.
                                                flip_draws[i],
                                                pCrtcPriv->flip_backpixs.
                                                flip_pixmaps[i]);
#endif

            (*pScreen->DestroyPixmap) (pCrtcPriv->flip_backpixs.
                                       flip_pixmaps[i]);
            pCrtcPriv->flip_backpixs.flip_pixmaps[i] = NULL;
            pCrtcPriv->flip_backpixs.flip_draws[i] = NULL;
        }
    }
    pCrtcPriv->flip_backpixs.lub = -1;
}

static void
EXYNOSDummyCrtcDpms(xf86CrtcPtr pCrtc, int pMode)
{
    XDBG_DEBUG(MDOUT, "\n");
}

static Bool
EXYNOSDummyCrtcSetModeMajor(xf86CrtcPtr pCrtc, DisplayModePtr pMode,
                            Rotation rotation, int x, int y)
{
    XDBG_DEBUG(MDOUT, "\n");
    ScrnInfoPtr pScrn = pCrtc->scrn;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSFbPtr pFb = pExynos->pFb;
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    int i = 0;

    if (pCrtcPriv == NULL) {
        return TRUE;
    }
    EXYNOSModePtr pExynosMode = pCrtcPriv->pExynosMode;
    tbm_bo bo = NULL, old_bo = NULL;
    tbm_bo bo_accessibility[2] = { 0, }
    , old_bo_accessibility[2] = {
    0,};
    int saved_x, saved_y;
    Rotation saved_rotation;
    DisplayModeRec saved_mode;

    XDBG_DEBUG(MDOUT,
               "SetModeMajor pMode:%d cur(%dx%d+%d+%d),rot:%d new(%dx%d+%d+%d),refresh(%f)rot:%d\n",
               exynosCrtcID(pCrtcPriv),
               pCrtc->mode.HDisplay, pCrtc->mode.VDisplay, pCrtc->x, pCrtc->y,
               pCrtc->rotation,
               pMode->HDisplay, pMode->VDisplay, x, y, pMode->VRefresh,
               rotation);

    memcpy(&saved_mode, &pCrtc->mode, sizeof(DisplayModeRec));
    saved_x = pCrtc->x;
    saved_y = pCrtc->y;
    saved_rotation = pCrtc->rotation;

    memcpy(&pCrtc->mode, pMode, sizeof(DisplayModeRec));
    pCrtc->x = x;
    pCrtc->y = y;
    pCrtc->rotation = rotation;

    if (pExynos->fake_root)
        exynosDisplaySwapModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, pMode);
    else
        exynosDisplayModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, pMode);

    /* accessibility */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        XDBG_GOTO_IF_FAIL(pCrtcPriv->accessibility_front_bo != NULL, fail);
        XDBG_GOTO_IF_FAIL(pCrtcPriv->accessibility_back_bo != NULL, fail);

        old_bo_accessibility[0] = pCrtcPriv->accessibility_front_bo;
        old_bo_accessibility[1] = pCrtcPriv->accessibility_back_bo;

        bo_accessibility[0] =
            exynosRenderBoCreate(pScrn, pMode->HDisplay, pMode->VDisplay);
        bo_accessibility[1] =
            exynosRenderBoCreate(pScrn, pMode->HDisplay, pMode->VDisplay);

        pCrtcPriv->accessibility_front_bo = bo_accessibility[0];
        pCrtcPriv->accessibility_back_bo = bo_accessibility[1];
    }

    /* find bo which covers the requested mode of crtc */
    old_bo = pCrtcPriv->front_bo;
    bo = exynosFbGetBo(pFb, x, y, pMode->HDisplay, pMode->VDisplay, FALSE);
    XDBG_GOTO_IF_FAIL(bo != NULL, fail);
    pCrtcPriv->front_bo = bo;
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        tbm_bo temp;

        bo = pCrtcPriv->accessibility_back_bo;
        temp = pCrtcPriv->accessibility_front_bo;
        pCrtcPriv->accessibility_front_bo = pCrtcPriv->accessibility_back_bo;
        pCrtcPriv->accessibility_back_bo = temp;
    }

    /* turn off the crtc if the same crtc is set already by another display mode
     * before the set crtcs
     */
    exynosDisplaySetDispSetMode(pScrn, DISPLAY_SET_MODE_OFF);

    if (!pCrtcPriv->onoff)
        exynosCrtcTurn(pCrtc, TRUE, FALSE, FALSE);

    /* for cache control */
    tbm_bo_map(bo, TBM_DEVICE_2D, TBM_OPTION_READ);
    tbm_bo_unmap(bo);
    for (i = 0; i < xf86_config->num_output; i++) {
        xf86OutputPtr pOutput = xf86_config->output[i];
        EXYNOSOutputPrivPtr pOutputPriv;

        if (pOutput->crtc != pCrtc)
            continue;

        pOutputPriv = pOutput->driver_private;

#if 1
        memcpy(&pExynosMode->main_lcd_mode, pOutputPriv->mode_output->modes,
               sizeof(drmModeModeInfo));
#endif

        pOutputPriv->dpms_mode = DPMSModeOn;
    }
    exynosOutputDrmUpdate(pScrn);
    if (pScrn->pScreen)
        xf86_reload_cursors(pScrn->pScreen);

#ifdef NO_CRTC_MODE
    pExynos->isCrtcOn = exynosCrtcCheckInUseAll(pScrn);
#endif
    /* set the default external mode */
    exynosDisplayModeToKmode(pCrtc->scrn, &pExynosMode->ext_connector_mode,
                             pMode);

    return TRUE;
 fail:
    XDBG_ERROR(MDOUT, "Fail crtc apply(crtc_id:%d, rotate:%d, %dx%d+%d+%d\n",
               exynosCrtcID(pCrtcPriv), rotation, x, y, pCrtc->mode.HDisplay,
               pCrtc->mode.VDisplay);

    pCrtcPriv->front_bo = old_bo;

    /* accessibility */
    if (pCrtcPriv->bAccessibility || pCrtcPriv->screen_rotate_degree > 0) {
        if (bo_accessibility[0])
            exynosRenderBoUnref(bo_accessibility[0]);
        if (bo_accessibility[1])
            exynosRenderBoUnref(bo_accessibility[1]);

        pCrtcPriv->accessibility_front_bo = old_bo_accessibility[0];
        pCrtcPriv->accessibility_back_bo = old_bo_accessibility[1];
    }

    if (pExynos->fake_root)
        exynosDisplaySwapModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode,
                                     &saved_mode);
    else
        exynosDisplayModeToKmode(pCrtc->scrn, &pCrtcPriv->kmode, &saved_mode);

    memcpy(&pCrtc->mode, &saved_mode, sizeof(DisplayModeRec));
    pCrtc->x = saved_x;
    pCrtc->y = saved_y;
    pCrtc->rotation = saved_rotation;

    return FALSE;
}

static void
EXYNOSDummyCrtcGammaSet(xf86CrtcPtr pCrtc,
                        CARD16 *red, CARD16 *green, CARD16 *blue, int size)
{
    XDBG_DEBUG(MDOUT, "\n");
}

static void
EXYNOSDummyCrtcDestroy(xf86CrtcPtr pCrtc)
{
    XDBG_DEBUG(MDOUT, "\n");
    EXYNOSCrtcPrivPtr pCrtcPriv = pCrtc->driver_private;

    if (pCrtcPriv == NULL) {
        return;
    }
    DRI2FrameEventPtr event_ref = NULL, event_next = NULL;

    xorg_list_for_each_entry_safe(event_ref, event_next,
                                  &pCrtcPriv->pending_flips,
                                  crtc_pending_link) {
        free(event_ref);
    }

    _dummyFlipPixmapDeinit(pCrtc);

#if 0
    if (pCrtcPriv->pFpsDebug) {
        xDbgLogFpsDebugDestroy(pCrtcPriv->pFpsDebug);
        pCrtcPriv->pFpsDebug = NULL;
    }
#endif

    if (pCrtcPriv->accessibility_front_bo) {
        exynosRenderBoUnref(pCrtcPriv->accessibility_front_bo);
        pCrtcPriv->accessibility_front_bo = NULL;
    }

    if (pCrtcPriv->accessibility_back_bo) {
        exynosRenderBoUnref(pCrtcPriv->accessibility_back_bo);
        pCrtcPriv->accessibility_back_bo = NULL;
    }

    if (pCrtcPriv->backup_image) {
        pixman_image_unref(pCrtcPriv->backup_image);
        pCrtcPriv->backup_image = NULL;
    }

    if (pCrtcPriv->ovl_vbuf_cursor) {
        exynosUtilVideoBufferUnref(pCrtcPriv->ovl_vbuf_cursor);
        pCrtcPriv->ovl_vbuf_cursor = NULL;
    }

    if (pCrtcPriv->ovl_vbuf_pixmap) {
        exynosUtilVideoBufferUnref(pCrtcPriv->ovl_vbuf_pixmap);
        pCrtcPriv->ovl_vbuf_pixmap = NULL;
    }

    if (pCrtcPriv->ovl_layer) {
        exynosLayerUnref(pCrtcPriv->ovl_layer);
        pCrtcPriv->ovl_layer = NULL;
    }

    if (pCrtcPriv->mode_crtc)
        drmModeFreeCrtc(pCrtcPriv->mode_crtc);

    if (pCrtcPriv->front_bo) {
        pCrtcPriv->front_bo = NULL;
    }

    if (pCrtcPriv->back_bo) {
        exynosRenderBoUnref(pCrtcPriv->back_bo);
        pCrtcPriv->back_bo = NULL;
    }

    if (pCrtcPriv->flip_backpixs.pix_free != NULL) {
        free(pCrtcPriv->flip_backpixs.pix_free);
        pCrtcPriv->flip_backpixs.pix_free = NULL;
    }

    if (pCrtcPriv->flip_backpixs.flip_pixmaps != NULL) {
        free(pCrtcPriv->flip_backpixs.flip_pixmaps);
        pCrtcPriv->flip_backpixs.flip_pixmaps = NULL;
    }

    if (pCrtcPriv->flip_backpixs.flip_draws != NULL) {
        free(pCrtcPriv->flip_backpixs.flip_draws);
        pCrtcPriv->flip_backpixs.flip_draws = NULL;
    }
    (pCrtcPriv->pExynosMode->num_dummy_crtc)--;
    xorg_list_del(&pCrtcPriv->link);
    free(pCrtcPriv);

    pCrtc->driver_private = NULL;
}

static xf86OutputStatus
EXYNOSDummyOutputDetect(xf86OutputPtr pOutput)
{
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
        return XF86OutputStatusUnknown;
    }
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);

    if (pExynos == NULL) {
        return XF86OutputStatusUnknown;
    }
    EXYNOSModePtr pExynosMode = pExynos->pExynosMode;

    if (pExynosMode == NULL) {
        return XF86OutputStatusUnknown;
    }
#if 0
    if (pOutput->randr_output && pOutput->randr_output->numUserModes) {
        xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pOutput->scrn);

        if (xf86_config->output[xf86_config->num_output - 1] == pOutput) {
            EXYNOSPtr pExynos = (EXYNOSPtr) pOutput->scrn->driverPrivate;

            exynosDummyOutputInit(pOutput->scrn, pExynos->pExynosMode, TRUE);
        }
    }
#endif
    EXYNOSOutputPrivPtr pCur = NULL, pNext = NULL;

    xorg_list_for_each_entry_safe(pCur, pNext, &pExynosMode->outputs, link) {
        if (pCur->is_dummy == FALSE &&
            pCur->mode_output->connection == DRM_MODE_CONNECTED) {
            /* TODO: Need to change flag useAsyncSwap not here */
            pExynos->useAsyncSwap = FALSE;
            return XF86OutputStatusDisconnected;
        }
    }
    pExynos->useAsyncSwap = TRUE;
    return XF86OutputStatusConnected;
}

static Bool
EXYNOSDummyOutputModeValid(xf86OutputPtr pOutput, DisplayModePtr pModes)
{
    int ret = MODE_OK;
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL) {
        ret = MODE_ERROR;
    }
    else if (pModes->type & M_T_DEFAULT)
        ret = MODE_BAD;
    XDBG_DEBUG(MDOUT, "ret = %d\n", ret);
    return ret;

}

static DisplayModePtr
EXYNOSDummyOutputGetModes(xf86OutputPtr pOutput)
{
    XDBG_DEBUG(MDOUT, "\n");
    DisplayModePtr Modes = NULL;
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL)
        return Modes;

    Modes = xf86ModesAdd(Modes, xf86CVTMode(1024, 768, 60, 0, 0));
    Modes = xf86ModesAdd(Modes, xf86CVTMode(720, 1280, 60, 0, 0));
    return Modes;
}

static void
EXYNOSDummyOutputDestory(xf86OutputPtr pOutput)
{
    XDBG_DEBUG(MDOUT, "\n");
    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

    if (pOutputPriv == NULL)
        return;
    EXYNOSPtr pExynos = EXYNOSPTR(pOutput->scrn);

    if (pExynos->resume_timer) {
        TimerFree(pExynos->resume_timer);
        pExynos->resume_timer = NULL;
    }

    if (pOutputPriv->mode_output) {
        if (pOutputPriv->mode_output->modes)
            free(pOutputPriv->mode_output->modes);
        free(pOutputPriv->mode_output);
    }

    if (pOutputPriv->mode_encoder)
        free(pOutputPriv->mode_encoder);

    (pExynos->pExynosMode->num_dummy_output)--;
    xorg_list_del(&pOutputPriv->link);
    free(pOutputPriv);
    pOutput->driver_private = NULL;
}

static void
EXYNOSDummyOutputDpms(xf86OutputPtr pOutput, int dpms)
{
    XDBG_DEBUG(MDOUT, "\n");
}

static void
EXYNOSDummyOutputCreateResources(xf86OutputPtr pOutput)
{
    XDBG_DEBUG(MDOUT, "\n");
}

static Bool
EXYNOSDummyOutputSetProperty(xf86OutputPtr pOutput, Atom property,
                             RRPropertyValuePtr value)
{
    XDBG_DEBUG(MDOUT, "\n");
    return TRUE;
}

static Bool
EXYNOSDummyOutputGetProperty(xf86OutputPtr pOutput, Atom property)
{
    XDBG_DEBUG(MDOUT, "\n");
    return FALSE;
}

static void
EXYNOSDummyCrtcSetCursorColors(xf86CrtcPtr pCrtc, int bg, int fg)
{
    XDBG_TRACE(MDOUT, "[%p]  \n", pCrtc);
}

static void
EXYNOSDummyCrtcSetCursorPosition(xf86CrtcPtr pCrtc, int x, int y)
{
    XDBG_TRACE(MDOUT, "[%p]  \n", pCrtc);
    return;
}

static void
EXYNOSDummyCrtcShowCursor(xf86CrtcPtr pCrtc)
{
    XDBG_TRACE(MDOUT, "[%p]  \n", pCrtc);
    return;
}

static void
EXYNOSDummyCrtcHideCursor(xf86CrtcPtr pCrtc)
{
    XDBG_TRACE(MDOUT, "[%p]  \n", pCrtc);
    return;
}

static void
EXYNOSDummyCrtcLoadCursorArgb(xf86CrtcPtr pCrtc, CARD32 *image)
{
    XDBG_TRACE(MDOUT, "[%p]  \n", pCrtc);
    return;
}

static const xf86CrtcFuncsRec exynos_crtc_dummy_funcs = {
    .dpms = EXYNOSDummyCrtcDpms,
    .set_mode_major = EXYNOSDummyCrtcSetModeMajor,
    .set_cursor_colors = EXYNOSDummyCrtcSetCursorColors,
    .set_cursor_position = EXYNOSDummyCrtcSetCursorPosition,
    .show_cursor = EXYNOSDummyCrtcShowCursor,
    .hide_cursor = EXYNOSDummyCrtcHideCursor,
    .load_cursor_argb = EXYNOSDummyCrtcLoadCursorArgb,
#if 0
    .shadow_create = EXYNOSCrtcShadowCreate,
    .shadow_allocate = EXYNOSCrtcShadowAllocate,
    .shadow_destroy = EXYNOSCrtcShadowDestroy,
#endif
    .gamma_set = EXYNOSDummyCrtcGammaSet,
    .destroy = EXYNOSDummyCrtcDestroy,
};

static const xf86OutputFuncsRec exynos_output_dummy_funcs = {
    .create_resources = EXYNOSDummyOutputCreateResources,
#ifdef RANDR_12_INTERFACE
    .set_property = EXYNOSDummyOutputSetProperty,
    .get_property = EXYNOSDummyOutputGetProperty,
#endif
    .dpms = EXYNOSDummyOutputDpms,
#if 0
    .save = drmmode_crt_save,
    .restore = drmmode_crt_restore,
    .mode_fixup = drmmode_crt_mode_fixup,
    .prepare = exynos_output_prepare,
    .mode_set = drmmode_crt_mode_set,
    .commit = exynos_output_commit,
#endif
    .detect = EXYNOSDummyOutputDetect,
    .mode_valid = EXYNOSDummyOutputModeValid,

    .get_modes = EXYNOSDummyOutputGetModes,
    .destroy = EXYNOSDummyOutputDestory
};

Bool
exynosDummyOutputInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode, Bool late)
{
    XDBG_DEBUG(MDOUT, "\n");
    xf86CrtcConfigPtr xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);
    xf86OutputPtr pOutput;
    xf86CrtcPtr pCrtc;
    EXYNOSOutputPrivPtr pOutputPriv;
    RROutputPtr clones[32];
    RRCrtcPtr crtcs[32];
    DisplayModePtr pModes = NULL;
    char buf[80];
    int i, len;

    if (pExynosMode->num_dummy_output >= 32)
        return FALSE;

    XDBG_DEBUG(MDISP, "(late=%d, num_dummy=%d)\n", late,
               pExynosMode->num_dummy_output + 1);

    len =
        snprintf(buf, sizeof(buf), "DUMMY%d",
                 pExynosMode->num_dummy_output + 1);
    pOutput = xf86OutputCreate(pScrn, &exynos_output_dummy_funcs, buf);
    if (!pOutput) {
        return FALSE;
    }

    pCrtc = exynosDummyCrtcInit(pScrn, pExynosMode);

    if (pCrtc == NULL) {
        xf86OutputDestroy(pOutput);
        return FALSE;
    }

    pOutputPriv = calloc(sizeof(EXYNOSOutputPrivRec), 1);
    if (!pOutputPriv) {
        xf86OutputDestroy(pOutput);
        xf86CrtcDestroy(pCrtc);
        return FALSE;
    }

    pOutput->mm_width = 0;
    pOutput->mm_height = 0;
    pOutput->interlaceAllowed = FALSE;
    pOutput->subpixel_order = SubPixelNone;

    pOutput->possible_crtcs = ~((1 << pExynosMode->num_real_crtc) - 1);
    pOutput->possible_clones = ~((1 << pExynosMode->num_real_output) - 1);
    pOutputPriv->is_dummy = TRUE;
    pOutputPriv->output_id = 1000 + pExynosMode->num_dummy_output;

    pOutputPriv->mode_output = calloc(1, sizeof(drmModeConnector));
    if (pOutputPriv->mode_output == NULL) {
        free(pOutputPriv);
        goto err;
    }
    pOutputPriv->mode_output->connector_type = DRM_MODE_CONNECTOR_Unknown;
    pOutputPriv->mode_output->connector_type_id =
        pExynosMode->num_dummy_output + 1;
    pOutputPriv->mode_output->connector_id =
        1000 + pExynosMode->num_dummy_output;
    pOutputPriv->mode_output->connection = DRM_MODE_UNKNOWNCONNECTION;
    pOutputPriv->mode_output->count_props = 0;
    pOutputPriv->mode_output->count_encoders = 0;
    pOutputPriv->mode_encoder = NULL;
    pOutputPriv->mode_output->count_modes = 1;
    pOutputPriv->mode_output->modes = calloc(2, sizeof(drmModeModeInfo));
    if (pOutputPriv->mode_output->modes == NULL) {
        free(pOutputPriv->mode_output);
        free(pOutputPriv);
        goto err;
    }
#if 0
    pOutputPriv->mode_encoder = calloc(1, sizeof(drmModeEncoder));
    if (pOutputPriv->mode_encoder == NULL)
        goto err;
    pOutputPriv->mode_encoder->possible_clones =
        ~((1 << pExynosMode->num_real_output) - 1);
    pOutputPriv->mode_encoder->possible_crtcs =
        ~((1 << pExynosMode->num_real_crtc) - 1);
    pOutputPriv->mode_encoder->encoder_type = DRM_MODE_ENCODER_NONE;
#endif
    pModes = xf86CVTMode(1024, 768, 60, 0, 0);
    exynosDisplayModeToKmode(pScrn, &pOutputPriv->mode_output->modes[0],
                             pModes);
    free(pModes);
    pOutputPriv->pExynosMode = pExynosMode;

    pOutput->driver_private = pOutputPriv;
    pOutputPriv->pOutput = pOutput;
    /* TODO : soolim : management crtc privates */
    xorg_list_add(&pOutputPriv->link, &pExynosMode->outputs);

    if (late) {
        ScreenPtr pScreen = xf86ScrnToScreen(pScrn);

        pCrtc->randr_crtc = RRCrtcCreate(pScreen, pCrtc);
        pOutput->randr_output = RROutputCreate(pScreen, buf, len, pOutput);
        if (pCrtc->randr_crtc == NULL || pOutput->randr_output == NULL) {
            xf86OutputDestroy(pOutput);
            xf86CrtcDestroy(pCrtc);
            return FALSE;
        }

        RRPostPendingProperties(pOutput->randr_output);

        for (i = pExynosMode->num_real_output; i < xf86_config->num_output; i++)
            clones[i - pExynosMode->num_real_output] =
                xf86_config->output[i]->randr_output;
        XDBG_RETURN_VAL_IF_FAIL(i - pExynosMode->num_real_output ==
                                pExynosMode->num_dummy_output + 1, FALSE);

        for (i = pExynosMode->num_real_crtc; i < xf86_config->num_crtc; i++)
            crtcs[i - pExynosMode->num_real_crtc] =
                xf86_config->crtc[i]->randr_crtc;
        XDBG_RETURN_VAL_IF_FAIL(i - pExynosMode->num_real_crtc ==
                                pExynosMode->num_dummy_output + 1, FALSE);

        for (i = pExynosMode->num_real_output; i < xf86_config->num_output; i++) {
            RROutputPtr rr_output = xf86_config->output[i]->randr_output;

            if (!RROutputSetCrtcs
                (rr_output, crtcs, pExynosMode->num_dummy_output + 1) ||
                !RROutputSetClones(rr_output, clones,
                                   pExynosMode->num_dummy_output + 1))
                goto err;
        }

        RRCrtcSetRotations(pCrtc->randr_crtc, RR_Rotate_All | RR_Reflect_All);
    }
    pExynosMode->num_dummy_output++;
    return TRUE;

 err:
    for (i = 0; i < xf86_config->num_output; i++) {
        pOutput = xf86_config->output[i];
        if (pOutput->driver_private == NULL)
            continue;
        pOutputPriv = (EXYNOSOutputPrivPtr) pOutput->driver_private;
        if (pOutputPriv->is_dummy) {
            xf86OutputDestroy(pOutput);
        }
    }

    for (i = 0; i < xf86_config->num_crtc; i++) {
        pCrtc = xf86_config->crtc[i];

        if (pCrtc->driver_private == NULL)
            continue;
        EXYNOSCrtcPrivPtr pCrtcPriv = (EXYNOSCrtcPrivPtr) pCrtc->driver_private;

        if (pCrtcPriv->is_dummy) {
            xf86CrtcDestroy(pCrtc);
        }
    }
    pExynosMode->num_dummy_output = 0;
    return FALSE;
}

xf86CrtcPtr
exynosDummyCrtcInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode)
{
    XDBG_DEBUG(MDOUT, "\n");
    xf86CrtcPtr pCrtc = NULL;

//    DisplayModePtr pModes = NULL;
    EXYNOSCrtcPrivPtr pCrtcPriv;

    pCrtcPriv = calloc(sizeof(EXYNOSCrtcPrivRec), 1);
    if (pCrtcPriv == NULL)
        return NULL;

    pCrtc = xf86CrtcCreate(pScrn, &exynos_crtc_dummy_funcs);
    if (pCrtc == NULL) {
        free(pCrtcPriv);
        return NULL;
    }
    pCrtcPriv->is_dummy = TRUE;
    pCrtcPriv->idx = 1000 + pExynosMode->num_dummy_crtc;
    pCrtcPriv->mode_crtc = calloc(1, sizeof(drmModeCrtc));
    if (!pCrtcPriv->mode_crtc) {
        free(pCrtcPriv);
        return NULL;
    }
#if 0
    pModes = xf86CVTMode(720, 1280, 60, 0, 0);
    exynosDisplayModeToKmode(pScrn, &pCrtcPriv->kmode, pModes);
    exynosDisplayModeToKmode(pScrn, &pCrtcPriv->mode_crtc->mode, pModes);
    free(pModes);
#endif
    /* TODO: Unique crtc_id */
    pCrtcPriv->mode_crtc->crtc_id = 1000 + pExynosMode->num_dummy_crtc;

    pCrtcPriv->move_layer = FALSE;
    pCrtcPriv->user_rotate = RR_Rotate_0;

    pCrtcPriv->pExynosMode = pExynosMode;
    pCrtc->driver_private = pCrtcPriv;

    pCrtcPriv->pipe = 1000 + pExynosMode->num_dummy_crtc;
    pCrtcPriv->onoff = TRUE;

    xorg_list_init(&pCrtcPriv->pending_flips);

    pCrtcPriv->pCrtc = pCrtc;

#ifdef USE_XDBG
    pCrtcPriv->pFpsDebug = xDbgLogFpsDebugCreate();
    if (pCrtcPriv->pFpsDebug == NULL) {
        free(pCrtcPriv);
        return NULL;
    }
#endif

    _dummyFlipPixmapInit(pCrtc);
    pExynosMode->num_dummy_crtc++;
    xorg_list_add(&(pCrtcPriv->link), &(pExynosMode->crtcs));
    return pCrtc;
}
#endif                          //NO_CRTC_MODE
