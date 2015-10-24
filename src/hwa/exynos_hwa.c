/* exynos-hwa.c
 *
 * Copyright (c) 2009, 2013 The Linux Foundation. All rights reserved.
 *
 * Contact: Oleksandr Rozov<o.rozov@samsung.com>
 * Contact: Roman Peresipkyn<r.peresipkyn@samsung.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_display.h"
#include <list.h>

#include "exa.h"
#include <hwa.h>
#include <randrstr.h>
#include "exynos_hwa.h"
#include "exynos_crtc.h"
#include "exynos_layer.h"

#include "xf86drm.h"

typedef enum {
    LVDS_LAYERS = 5,
    HDMI_LAYERS = 3,
    VIRTUAL_LAYERS = 3
} HWA_OUTPUT_LAYERS;

static xf86OutputPtr pOutputxf = NULL;

static xf86CrtcPtr
_exynosHwaPreparePtrCrtcFromPtrScreen(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = NULL;

    xf86OutputPtr pOutput = NULL;
    xf86CrtcConfigPtr xf86_config = NULL;

    BOOL found = FALSE;

    pScrn = xf86Screens[pScreen->myNum];
    xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

    int i = 0;

    for (i = 0; i < xf86_config->num_output; i++) {
        pOutput = xf86_config->output[i];
        if (!pOutput->crtc->enabled)
            continue;

        if (!strcmp(pOutput->name, "LVDS1")) {
            found = TRUE;
            break;
        }
    }

    if (!found)
        return NULL;

    return pOutput->crtc;
}

static void *
_exynosFindOutputLayersFromID(ScreenPtr pScreen, RRCrtc rcrtc_id)
{
    int i = 0;
    ScrnInfoPtr pScrn = NULL;
    xf86OutputPtr pOutput = NULL;
    xf86CrtcConfigPtr xf86_config = NULL;

    RRCrtcPtr pRandrCrtc = NULL;

    pScrn = xf86Screens[pScreen->myNum];
    xf86_config = XF86_CRTC_CONFIG_PTR(pScrn);

    for (i = 0; i < xf86_config->num_output; i++) {
        pOutput = xf86_config->output[i];
        if (!pOutput->crtc)
            continue;
        if (!pOutput->crtc->enabled)
            continue;

        pRandrCrtc = pOutput->crtc->randr_crtc;

        if (!pRandrCrtc)
            continue;
        if (pRandrCrtc->id == rcrtc_id) {
            pOutputxf = pOutput;
            return pOutput;
        }
    }

    return NULL;
}

static int
SecHwaOverlayShowLayer(ScreenPtr pScreen, RRCrtc rcrtc_id,
                       HWA_OVERLAY_LAYER ovrlayer)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    uint16_t nlayers = 0;
    EXYNOSLayerPtr curlayer = NULL;

    /* check active connector attached to crtc */
    if (!_exynosFindOutputLayersFromID(pScreen, rcrtc_id))
        return BadRequest;

    /* find layers on this crtc */
    switch (ovrlayer) {
    case HWA_OVERLAY_LAYER_UI:

        if (!strcmp(pOutputxf->name, "LVDS1")) {

            curlayer = exynosLayerFind(LAYER_OUTPUT_LCD, LAYER_LOWER1 + 1);
            nlayers = LVDS_LAYERS;
            break;
        }

        if (!strcmp(pOutputxf->name, "HDMI1")) {

            curlayer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1 + 1);
            nlayers = HDMI_LAYERS;
            break;
        }

        if (!strcmp(pOutputxf->name, "Virtual1")) {

            curlayer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1 + 1);
            nlayers = VIRTUAL_LAYERS;
            break;
        }

        if (!curlayer) {
            XDBG_DEBUG(MHWA, "UI layer doesn't exists!\n");
            return BadRequest;
        }

        exynosLayerHide(curlayer);
        XDBG_DEBUG(MHWA, "UI layer[%d] is shown now!\n layers on output= %d\n",
                   (int) exynosLayerGetPos(curlayer), nlayers);
        break;

    case HWA_OVERLAY_LAYER_XV:

        pExynos->XVHIDE = 0;
        XDBG_DEBUG(MHWA, "XV layer is shown now!\n");
        break;

    default:
        break;
    }

    return Success;
}

static int
SecHwaOverlayHideLayer(ScreenPtr pScreen, RRCrtc rcrtc_id,
                       HWA_OVERLAY_LAYER ovrlayer)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    uint16_t nlayers = 0;
    EXYNOSLayerPtr curlayer = NULL;

    /* check active connector attached to crtc */
    if (!_exynosFindOutputLayersFromID(pScreen, rcrtc_id))
        return BadRequest;

    /* find layers on this crtc */
    switch (ovrlayer) {
    case HWA_OVERLAY_LAYER_UI:

        if (!strcmp(pOutputxf->name, "LVDS1")) {

            curlayer = exynosLayerFind(LAYER_OUTPUT_LCD, LAYER_LOWER1);
            nlayers = LVDS_LAYERS;
            break;
        }

        if (!strcmp(pOutputxf->name, "HDMI1")) {

            curlayer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1);
            nlayers = HDMI_LAYERS;
            break;
        }

        if (!curlayer) {
            XDBG_DEBUG(MHWA, "UI layer doesn't exists!\n");
            return BadRequest;
        }

        exynosLayerHide(curlayer);
        XDBG_DEBUG(MHWA, "UI layer[%d] is hidden now!\n layers on output=%d\n",
                   (int) exynosLayerGetPos(curlayer), nlayers);
        break;

    case HWA_OVERLAY_LAYER_XV:
        pExynos->XVHIDE = 1;
        XDBG_DEBUG(MHWA, "XV layer is hidden now!\n");
        break;
    default:
        break;
    }

    return Success;
}

static int
SecHwaCursorEnable(ScreenPtr pScreen, CARD16 *pEnable)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int bEnable = *pEnable;

    if (bEnable != pExynos->enableCursor) {
        pExynos->enableCursor = bEnable;
        if (exynosCrtcCursorEnable(pScrn, bEnable)) {
            *pEnable = 1;
            XDBG_DEBUG(MHWA, "Xorg] cursor %s.\n",
                       bEnable ? "enable" : "disable\n");

        }
        else {
            *pEnable = 0;
            XDBG_DEBUG(MHWA, "[Xorg] Fail cursor %s.\n",
                       bEnable ? "enable" : "disable");
        }
    }
    else {
        *pEnable = bEnable;
        XDBG_DEBUG(MHWA, "[Xorg] already cursor %s.\n",
                   bEnable ? "enabled" : "disabled");
    }

    return Success;
}

static int
SecHwaCursorRotate(ScreenPtr pScreen, RRCrtc rcrtc_id, CARD16 *pdegree)
{
    int RR_rotate;
    int rotate = *pdegree;

    /* rcrtc_id>=0 ? */
    if (!rcrtc_id)
        return BadRequest;

    if (!_exynosFindOutputLayersFromID(pScreen, rcrtc_id))
        return BadRequest;

    RR_rotate = exynosUtilDegreeToRotate(rotate);
    if (!RR_rotate) {
        XDBG_DEBUG(MHWA, "[Xorg] Not support rotate(0, 90, 180, 270 only)\n");
        return BadRequest;
    }
    if (exynosCrtcCursorRotate(pOutputxf->crtc, RR_rotate)) {
        XDBG_DEBUG(MHWA, "[Xorg] cursor rotated %d.\n", rotate);
        *pdegree = rotate;
    }
    else {
        XDBG_DEBUG(MHWA, "[Xorg] Fail cursor rotate %d.\n", rotate);
        return BadRequest;
    }

    return Success;
}

static int
EXYNOSScreenInvertNegative(ScreenPtr pScreen, int accessibility_status)
{
    xf86CrtcPtr pCrtc = NULL;

    pCrtc = _exynosHwaPreparePtrCrtcFromPtrScreen(pScreen);
    if (!pCrtc)
        return BadAlloc;

    EXYNOSCrtcPrivPtr pCrtcPriv = (EXYNOSCrtcPrivPtr) pCrtc->driver_private;

    /* TODO process a case when option is already enabled\disabled
       if( accessibility_status == pCrtcPriv->accessibility_status )
       return Success;
     */

    pCrtcPriv->accessibility_status = accessibility_status;

    exynosCrtcEnableAccessibility(pCrtc);

    return Success;
}

static int
EXYNOSScreenScale(ScreenPtr pScreen, int scale_status, int x, int y, int w,
                  int h)
{
    xf86CrtcPtr pCrtc = NULL;
    int output_w = 0, output_h = 0;

    pCrtc = _exynosHwaPreparePtrCrtcFromPtrScreen(pScreen);
    if (!pCrtc)
        return BadAlloc;

    EXYNOSCrtcPrivPtr pCrtcPriv = (EXYNOSCrtcPrivPtr) pCrtc->driver_private;

    /* TODO process a case when option is already enabled\disabled
       if( scale_status == pCrtcPriv->bScale )
       return Success;
     */

    pCrtcPriv->bScale = scale_status;

    output_w = pCrtc->mode.HDisplay;
    output_h = pCrtc->mode.VDisplay;

    if (pCrtcPriv->bScale) {
        /*Check invalidate region */
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (x + w > output_w)
            w = output_w - x;
        if (y + h > output_h)
            h = output_h - y;

        if (pCrtcPriv->rotate == RR_Rotate_90) {
            pCrtcPriv->sx = y;
            pCrtcPriv->sy = output_w - (x + w);
            pCrtcPriv->sw = h;
            pCrtcPriv->sh = w;
        }
        else if (pCrtcPriv->rotate == RR_Rotate_270) {
            pCrtcPriv->sx = output_h - (y + h);
            pCrtcPriv->sy = x;
            pCrtcPriv->sw = h;
            pCrtcPriv->sh = w;
        }
        else if (pCrtcPriv->rotate == RR_Rotate_180) {
            pCrtcPriv->sx = output_w - (x + w);
            pCrtcPriv->sy = output_h - (y + h);
            pCrtcPriv->sw = w;
            pCrtcPriv->sh = h;
        }
        else {
            pCrtcPriv->sx = x;
            pCrtcPriv->sy = y;
            pCrtcPriv->sw = w;
            pCrtcPriv->sh = h;
        }
    }

    exynosCrtcEnableAccessibility(pCrtc);

    return Success;
}

Bool
exynosHwaInit(ScreenPtr pScreen)
{
    hwa_screen_info_ptr pHwaInfo = NULL;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    pHwaInfo = calloc(1, sizeof(hwa_screen_info_rec));
    XDBG_RETURN_VAL_IF_FAIL(pHwaInfo != NULL, FALSE);

    /* hwa_screen_info_ptr is a DDX callbacks which are used by HWA Extension */
    pHwaInfo->version = HWA_SCREEN_INFO_VERSION;
    pHwaInfo->cursor_enable = SecHwaCursorEnable;
    pHwaInfo->cursor_rotate = SecHwaCursorRotate;
    pHwaInfo->show_layer = SecHwaOverlayShowLayer;
    pHwaInfo->hide_layer = SecHwaOverlayHideLayer;
    pHwaInfo->screen_invert_negative = EXYNOSScreenInvertNegative;
    pHwaInfo->screen_scale = EXYNOSScreenScale;

    if (LoaderSymbol("hwa_screen_init")) {
        if (!hwa_screen_init(pScreen, pHwaInfo)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "[HWA] hwa_screen_init failed.\n");
            goto fail_init;
        }
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[HWA] hwa_screen_init not exist. XServer doesn't support HWA extension\n");
        goto fail_init;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWA] Enable HWA.\n");

    return TRUE;

 fail_init:

    if (pHwaInfo)
        free(pHwaInfo);

    return FALSE;
}

void
exynosHwaDeinit(ScreenPtr pScreen)
{
    XDBG_INFO(MHWC, "Close HWA.\n");

}
