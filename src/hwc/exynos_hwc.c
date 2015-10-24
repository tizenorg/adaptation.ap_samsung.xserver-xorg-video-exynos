/* exynos-hwc.c
 *
 * Copyright (c) 2009, 2013 The Linux Foundation. All rights reserved.
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
#include <hwc.h>
#include "exynos_hwc.h"

#include "xf86drm.h"

#include "exynos_layer_manager.h"

#define EXYNOS_HWC_MAX_LAYER 4

static RESTYPE layers_rtype;

static EXYNOSLayerOutput outputlayer = LAYER_OUTPUT_LCD;        //by default LCD only

struct _exynosHwcRec {
    ScreenPtr pScreen;

    /* number of layers used by x client */
    int layer_count;
    /* number of available layers for x client to use */
    int max_layers;

    EXYNOSLayerMngClientID lyr_client_id;

    /* drawable id associated with this layer */
    DrawablePtr pDraw[EXYNOS_HWC_MAX_LAYER];
    int lpos[EXYNOS_HWC_MAX_LAYER];
    xRectangle src_rect[EXYNOS_HWC_MAX_LAYER];
    xRectangle dst_rect[EXYNOS_HWC_MAX_LAYER];
    struct xorg_list used_dri_list;
};

typedef struct {
    DrawablePtr pDraw;
    struct xorg_list used_dri_link;
} used_dri;

static void
_hwcSaveDrawable(ScrnInfoPtr pScrn, DrawablePtr pDraw, int num, int index,
                 int type)
{
    ScreenPtr pScreen = pDraw->pScreen;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    char file[128];
    char *str_type[2] = { "none", "SetDrawable" };
    PixmapPtr pPix = NULL;
    EXYNOSPixmapPriv *pExaPixPriv = NULL;

    if (!pExynos->dump_info || !(pExynos->dump_mode & XBERC_DUMP_MODE_HWC))
        return;

    XDBG_RETURN_IF_FAIL(pDraw != NULL);

    if (pDraw->type == DRAWABLE_WINDOW)
        pPix = (*pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    else
        pPix = (PixmapPtr) pDraw;

    pExaPixPriv = exaGetPixmapDriverPrivate(pPix);
    snprintf(file, sizeof(file), "[HWC]%s_%x_%p_{%d,%d}_%03d.%s",
             str_type[type],
             (unsigned int) pDraw->id,
             (void *) pPix, index, num, pExaPixPriv->dump_cnt,
             pExynos->dump_type);

    exynosUtilDoDumpPixmaps(pExynos->dump_info, pPix, file, pExynos->dump_type);

    XDBG_DEBUG(MSEC, "dump done\n");

    pExaPixPriv->dump_cnt++;
}

static int
_exynosOverlayRegisterEventDrawableGone(void *data, XID id)
{
    EXYNOSHwcDrawableInfo *hdi = (EXYNOSHwcDrawableInfo *) data;

    XDBG_RETURN_VAL_IF_FAIL(hdi != NULL, BadValue);
    EXYNOSHwcPtr pHwc = EXYNOSPTR(hdi->pScrn)->pHwc;

    XDBG_RETURN_VAL_IF_FAIL(pHwc != NULL, BadValue);
    XDBG_WARNING(MHWC, "Drawable id[%p] has been deleted\n", id);

    int i;

    for (i = 0; i < EXYNOS_HWC_MAX_LAYER; i++) {
        if (pHwc->pDraw[i] != NULL) {
            if (pHwc->pDraw[i]->id == id) {
                exynosHwcSetDriFlag(pHwc->pDraw[i], FALSE);
                exynosLayerMngRelease(pHwc->lyr_client_id, outputlayer,
                                      pHwc->lpos[i]);
                pHwc->pDraw[i] = NULL;
                pHwc->lpos[i] = LAYER_NONE;
            }
        }
    }

    free(hdi);
    return Success;
}

static Bool
_exynosOverlayRegisterEventResourceTypes(void)
{
    layers_rtype =
        CreateNewResourceType(_exynosOverlayRegisterEventDrawableGone,
                              "EXYNOS Hwc Overlay Drawable");

    if (!layers_rtype)
        return FALSE;

    return TRUE;
}

/*
 * Add layer_rtype to drawable
 * When drawable is destroyed, _exynosOverlayRegisterEventDrawableGone
 * will be called and if that  drawable is on overlay list. it will be destroyed..
 */
Bool
_attachCallbackToDraw(DrawablePtr pDraw, EXYNOSLayerPtr pLayer,
                      ScrnInfoPtr pScrn)
{
    EXYNOSHwcDrawableInfo *resource;

    resource = malloc(sizeof(EXYNOSHwcDrawableInfo));
    if (resource == NULL)
        return FALSE;

    if (!AddResource(pDraw->id, layers_rtype, resource)) {
        free(resource);
        return FALSE;
    }

    resource->type = layers_rtype;
    resource->pScrn = pScrn;

    return TRUE;
}

/*
 * The first drawable passing through EXYNOSHwcSetDrawables should be placed into top layer.
 * In order that the lower layers were visible the user has to take care independently of that
 * the area over the lower layer would be transparent. It to get capability
 * to hide a layer without calling of EXYNOSHwcSetDrawables and to avoid a blinking.
 */
static Bool
_exynosHwcDoSetDrawables(EXYNOSHwcPtr pHwc, DrawablePtr *pDraws,
                         xRectangle *srcRects, xRectangle *dstRects, int count)
{
    ScreenPtr pScreen = pHwc->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int lpos[EXYNOS_HWC_MAX_LAYER] = { 0 };
    int max_lpos;
    int i;

    max_lpos =
        exynosLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer,
                                             lpos);
    if (max_lpos < count) {
        XDBG_ERROR(MHWC, "Count of drawables:%d more of available planes:%d\n",
                   count, max_lpos);
        return FALSE;
    }
    pHwc->max_layers = max_lpos;

    //set a tracking function to the new drawable
    for (i = 0; i < count; i++) {
        if (!exynosHwcIsDrawExist(pDraws[i]))
            _attachCallbackToDraw(pDraws[i], NULL, pScrn);
    }

    /* reassign count to currently available number of layers */
    for (i = 0; i < count; i++) {
        if (pDraws[i] && pHwc->pDraw[i] && pDraws[i]->id != pHwc->pDraw[i]->id) {
            exynosLayerMngClearQueue(pHwc->lyr_client_id, outputlayer,
                                     pHwc->lpos[i]);
            exynosHwcSetDriFlag(pHwc->pDraw[i], FALSE);
            exynosHwcSetDriFlag(pDraws[i], FALSE);
        }
        if (exynosHwcGetDriFlag(pDraws[i])) {
            XDBG_GOTO_IF_FAIL(exynosLayerMngSet
                              (pHwc->lyr_client_id, 0, 0, &srcRects[i],
                               &dstRects[i], NULL, NULL, outputlayer,
                               lpos[(max_lpos - 1) - i], NULL, NULL),
                              fail_setdrwbs);
        }
        else {
            XDBG_GOTO_IF_FAIL(exynosLayerMngSet
                              (pHwc->lyr_client_id, 0, 0, &srcRects[i],
                               &dstRects[i], pDraws[i], NULL, outputlayer,
                               lpos[(max_lpos - 1) - i], NULL, NULL),
                              fail_setdrwbs);
        }
        pHwc->pDraw[i] = pDraws[i];
        pHwc->lpos[i] = lpos[(max_lpos - 1) - i];
        pHwc->src_rect[i] = srcRects[i];
        pHwc->dst_rect[i] = dstRects[i];
    }

    /* Remove old */
    for (i = count; i < EXYNOS_HWC_MAX_LAYER; i++) {
        if (pHwc->pDraw[i]) {
            exynosHwcSetDriFlag(pHwc->pDraw[i], FALSE);
            exynosLayerMngRelease(pHwc->lyr_client_id, outputlayer,
                                  pHwc->lpos[i]);
            pHwc->lpos[i] = LAYER_NONE;
            pHwc->pDraw[i] = NULL;
        }
    }

    /* set count of layers to the hwc */
    pHwc->layer_count = count;

    return TRUE;

 fail_setdrwbs:
    XDBG_ERROR(MHWC, "HWC set drawables failed\n");

    for (i = 0; i < count; i++) {
        exynosLayerMngRelease(pHwc->lyr_client_id, outputlayer,
                              lpos[(max_lpos - 1) - i]);
    }

    return FALSE;
}

Bool
exynosHwcDoSetRootDrawables(EXYNOSHwcPtr pHwc, PixmapPtr pScreenPixmap)
{
    Bool ret = FALSE;

    WindowPtr root = pHwc->pScreen->root;
    DrawablePtr pDrawRoot = &(root->drawable);
    xRectangle srcRect, dstRect;

    dstRect.x = pDrawRoot->x;
    dstRect.y = pDrawRoot->y;
    dstRect.width = pDrawRoot->width;
    dstRect.height = pDrawRoot->height;
    srcRect = dstRect;

    ret =
        _exynosHwcDoSetDrawables(pHwc, (DrawablePtr *) &pScreenPixmap, &dstRect,
                                 &srcRect, 1);

    return ret;
}

#ifdef USE_PIXMAN_COMPOSITE
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
#endif

PixmapPtr
exynosHwcGetScreenPixmap(ScreenPtr pScreen)
{
#ifdef USE_PIXMAN_COMPOSITE
    XDBG_DEBUG(MHWC,
               "================> merge layers to pixmap of root window.  \n");

    EXYNOSPtr pExynos = EXYNOSPTR(xf86ScreenToScrn(pScreen));
    EXYNOSHwcPtr pHwc = pExynos->pHwc;

    pixman_image_t *pSrcImg = NULL;
    pixman_image_t *pDstImg = NULL;

    PixmapPtr pDstPixmap = NULL;
    PixmapPtr pSrcPixmap = NULL;
    DrawablePtr pSrcDraw = NULL;

    EXYNOSPixmapPriv *pSrcPixmapPriv = NULL;
    EXYNOSPixmapPriv *pDstPixmapPriv = NULL;

    uint32_t *pSrcBits = NULL;
    uint32_t *pDstbits = NULL;

    Bool need_finish = FALSE;

    int i = 0;
    int pixman_op = PIXMAN_OP_SRC;

    /*
     * Step 1. Initialize dest pixmap private data.
     *             - It will be a root screen pixmap.
     */
    pDstPixmap = (*pScreen->GetScreenPixmap) (pScreen);
    XDBG_GOTO_IF_FAIL(pDstPixmap != NULL, fail_mergesrn);
    pDstPixmapPriv = exaGetPixmapDriverPrivate(pDstPixmap);
    XDBG_GOTO_IF_FAIL(pDstPixmapPriv != NULL, fail_mergesrn);
    if (!pDstPixmapPriv->bo) {
        need_finish = TRUE;
        exynosExaPrepareAccess(pDstPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL(pDstPixmapPriv->bo != NULL, fail_mergesrn);
    }

    pDstbits =
        tbm_bo_map(pDstPixmapPriv->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE).ptr;
    pDstImg =
        pixman_image_create_bits(PIXMAN_a8r8g8b8, pDstPixmap->drawable.width,
                                 pDstPixmap->drawable.height, pDstbits,
                                 pDstPixmap->devKind);

    /*
     * Step 2. Recursive composite between hwc layer's pixmap and root screen pixmap
     *             - The result is stored in root screen pixmap (dest pixmap).
     */
    for (i = 0; i < pHwc->layer_count; i++) {
        if (!pHwc->pDraw[i] || pHwc->dst_rect[i].width == 0 ||
            pHwc->dst_rect[i].height == 0) {
            continue;
        }
        pSrcDraw = pHwc->pDraw[i];
        XDBG_GOTO_IF_FAIL(pSrcDraw != NULL, fail_mergesrn);
        pSrcPixmap = _getPixmapFromDrawable(pHwc->pDraw[i]);
        XDBG_GOTO_IF_FAIL(pSrcPixmap != NULL, fail_mergesrn);

        pSrcPixmapPriv = exaGetPixmapDriverPrivate(pSrcPixmap);
        if (pDstPixmap == pSrcPixmap)
            continue;
        XDBG_GOTO_IF_FAIL(pSrcPixmapPriv != NULL, fail_mergesrn);
        XDBG_GOTO_IF_FAIL(pSrcPixmapPriv->bo != NULL, fail_mergesrn);

        pSrcBits =
            tbm_bo_map(pSrcPixmapPriv->bo, TBM_DEVICE_CPU, TBM_OPTION_READ).ptr;
        pSrcImg =
            pixman_image_create_bits(PIXMAN_a8r8g8b8,
                                     pSrcPixmap->drawable.width,
                                     pSrcPixmap->drawable.height, pSrcBits,
                                     pSrcPixmap->devKind);
        XDBG_GOTO_IF_FAIL(pSrcImg != NULL, fail_mergesrn);
        XDBG_GOTO_IF_FAIL(pDstImg != NULL, fail_mergesrn);

        /*
         * for base layer(first layer) PIXMAN_OP_SRC should be used
         */
        if (pSrcDraw->depth == 32 && i != 0)
            pixman_op = PIXMAN_OP_OVER;
        else
            pixman_op = PIXMAN_OP_SRC;

        XDBG_DEBUG(MHWC,
                   "pixmap operation(%d): layer_idx=%d [%d,%d %dx%d] => [%d,%d %dx%d]\n",
                   pixman_op, i, 0, 0, pSrcPixmap->drawable.width,
                   pSrcPixmap->drawable.height, pSrcPixmap->drawable.x,
                   pSrcPixmap->drawable.y, pDstPixmap->drawable.width,
                   pDstPixmap->drawable.height);

        xRectangle destRect = { 0, };
        memcpy(&destRect, &pHwc->dst_rect[i], sizeof(xRectangle));

        pixman_image_composite(pixman_op, pSrcImg, NULL, pDstImg, 0, 0, 0, 0,
                               pSrcPixmap->screen_x + destRect.x,
                               pSrcPixmap->screen_y + destRect.y,
                               pSrcPixmap->drawable.width,
                               pSrcPixmap->drawable.height);
        if (pSrcBits != NULL) {
            tbm_bo_unmap(pSrcPixmapPriv->bo);
            pSrcBits = NULL;
        }
    }

    XDBG_DEBUG(MHWC, "pixman composite is done\n");

 fail_mergesrn:

    if (pDstPixmap && need_finish)
        exynosExaFinishAccess(pDstPixmap, EXA_PREPARE_DEST);

    if (pSrcImg) {
        pixman_image_unref(pSrcImg);
        pSrcImg = NULL;
    }

    if (pDstbits != NULL) {
        tbm_bo_unmap(pDstPixmapPriv->bo);
        pDstbits = NULL;
    }

    if (pDstImg) {
        pixman_image_unref(pDstImg);
        pDstImg = NULL;
    }

    return pDstPixmap;
#else
    return (*pScreen->GetScreenPixmap) (pScreen);
#endif
}

static Bool
_exynosHwcSetValid(DrawablePtr *pDraws, xRectangle *srcRects,
                   xRectangle *dstRects, int count)
{
    DrawablePtr pDraw = NULL;
    xRectangle srcRect, dstRect;
    int i;

    XDBG_RETURN_VAL_IF_FAIL(pDraws != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(srcRects != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dstRects != NULL, FALSE);

    for (i = 0; i < count; i++) {
        pDraw = pDraws[i];
        srcRect = srcRects[i];
        dstRect = dstRects[i];
        XDBG_GOTO_IF_FAIL(pDraw->x >= 0, fail);
        XDBG_GOTO_IF_FAIL(pDraw->y >= 0, fail);
        XDBG_GOTO_IF_FAIL(pDraw->width > 0, fail);
        XDBG_GOTO_IF_FAIL(pDraw->height > 0, fail);

        XDBG_GOTO_IF_FAIL(srcRect.x >= 0, fail);
        XDBG_GOTO_IF_FAIL(srcRect.y >= 0, fail);
        XDBG_GOTO_IF_FAIL(srcRect.width > 0, fail);
        XDBG_GOTO_IF_FAIL(srcRect.width + srcRect.x <= pDraws[i]->width, fail);
        XDBG_GOTO_IF_FAIL(srcRect.height > 0, fail);
        XDBG_GOTO_IF_FAIL(srcRect.height + srcRect.y <= pDraws[i]->height,
                          fail);
        XDBG_GOTO_IF_FAIL(dstRect.x >= 0, fail);
        XDBG_GOTO_IF_FAIL(dstRect.y >= 0, fail);
        XDBG_GOTO_IF_FAIL(dstRect.width > 0, fail);
        XDBG_GOTO_IF_FAIL(dstRect.height > 0, fail);

        XDBG_DEBUG(MHWC,
                   "xid=%p pDraw(x,y,w,h)=(%d,%d,%d,%d) pSrcRect(%d,%d,%d,%d) pDstRect(%d,%d,%d,%d)\n",
                   (void *) pDraw->id, pDraw->x, pDraw->y, pDraw->width,
                   pDraw->height, srcRect.x, srcRect.y, srcRect.width,
                   srcRect.height, dstRect.x, dstRect.y, dstRect.width,
                   dstRect.height);
    }

    return TRUE;
 fail:
    XDBG_ERROR(MHWC,
               "Error: Drawable is not valid. ==> xid=%p pDraw(x,y,w,h)=(%d,%d,%d,%d) pSrcRect(%d,%d,%d,%d) pDstRect(%d,%d,%d,%d)\n",
               (void *) pDraw->id, pDraw->x, pDraw->y, pDraw->width,
               pDraw->height, srcRect.x, srcRect.y, srcRect.width,
               srcRect.height, dstRect.x, dstRect.y, dstRect.width,
               dstRect.height);

    return FALSE;
}

#ifdef USE_HWC_RESIZE_MOVE
static void
EXYNOSHwcMoveDrawable(ScreenPtr pScreen, DrawablePtr pDraw, int x, int y)
{
}

static void
EXYNOSHwcResizeDrawable(ScreenPtr pScreen, DrawablePtr pDraw, int x, int y,
                        int w, int h)
{
}
#endif

static int
EXYNOSHwcOpen(ScreenPtr pScreen, int *maxLayer)
{
    XDBG_DEBUG(MHWC, "enter! \n");

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSHwcPtr pHwc = pExynos->pHwc;

    *maxLayer = pHwc->max_layers;
    return Success;
}

int
EXYNOSHwcSetDrawables(ScreenPtr pScreen, DrawablePtr *pDraws,
                      xRectangle *srcRects, xRectangle *dstRects, int count)
{
    XDBG_DEBUG(MHWC, "================> Start set drawables. count:%d\n",
               count);

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSHwcPtr pHwc = pExynos->pHwc;
    int i;

    if (count == 0) {
        EXYNOSPTR(pScrn)->hwc_active = TRUE;
//        EXYNOSPTR(pScrn)->hwc_use_def_layer = TRUE;

        PixmapPtr pScreenPixmap = NULL;

        /*merge all layers to root */
        pScreenPixmap = exynosHwcGetScreenPixmap(pScreen);
        if (pScreenPixmap == NULL) {
            XDBG_ERROR(MHWC, "exynosHwcMergeScreenPixmap failed.\n");
            return BadMatch;
        }

        if (!exynosHwcDoSetRootDrawables(pHwc, pScreenPixmap)) {
            XDBG_WARNING(MHWC, "exynosHwcDoSetRootDrawables failed.\n");
            return BadMatch;
        }
        return Success;
    }

    if (!_exynosHwcSetValid(pDraws, srcRects, dstRects, count))
        return BadMatch;

    EXYNOSPTR(pScrn)->hwc_active = TRUE;
    EXYNOSPTR(pScrn)->hwc_use_def_layer = FALSE;

    if (!_exynosHwcDoSetDrawables(pHwc, pDraws, srcRects, dstRects, count)) {
        XDBG_WARNING(MHWC, "_exynosHwcDoSetDrawables failed.\n");
        return BadMatch;
    }

    for (i = 0; i < count; i++)
        _hwcSaveDrawable(pScrn, pDraws[i], count, i, 1);

    return Success;
}

static void
_exynosLayerAnnexHandler(void *user_data,
                         EXYNOSLayerMngEventCallbackDataPtr data)
{
    EXYNOSHwcPtr pHwc = (EXYNOSHwcPtr) user_data;
    int new_max_layers =
        exynosLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer,
                                             NULL);
    int old_max_layers = pHwc->max_layers;

    if (pHwc->layer_count <= new_max_layers) {
        _exynosHwcDoSetDrawables(pHwc, pHwc->pDraw, pHwc->src_rect,
                                 pHwc->dst_rect, pHwc->layer_count);
    }
    /* send configure notify */
    if (new_max_layers != old_max_layers) {
        pHwc->max_layers = new_max_layers;
        hwc_send_config_notify(pHwc->pScreen, pHwc->max_layers);
        XDBG_DEBUG(MHWC, "hwc_send_config_notify - pHwc->max_layers(%d)\n",
                   pHwc->max_layers);
    }
}

static void
_exynosLayerFreeCounterHandler(void *user_data,
                               EXYNOSLayerMngEventCallbackDataPtr data)
{
    EXYNOSHwcPtr pHwc = (EXYNOSHwcPtr) user_data;
    int new_max_layers =
        exynosLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer,
                                             NULL);

    if (pHwc->max_layers != new_max_layers) {
        pHwc->max_layers = new_max_layers;
        /* send configure notify */
        hwc_send_config_notify(pHwc->pScreen, pHwc->max_layers);
        XDBG_DEBUG(MHWC, "hwc_send_config_notify - pHwc->max_layers(%d)\n",
                   pHwc->max_layers);
    }
}

static void
_exynosUsedDri2InitList(EXYNOSHwcPtr pHwc)
{
    static Bool dri_list_init = FALSE;

    if (!dri_list_init) {
        xorg_list_init(&pHwc->used_dri_list);
        dri_list_init = TRUE;
    }
}

void
exynosHwcUpdate(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSHwcPtr pHwc = pExynos->pHwc;
    int i;

    for (i = 0; i < EXYNOS_HWC_MAX_LAYER; i++) {
        if (pHwc->pDraw[i]) {
            if (exynosHwcGetDriFlag(pHwc->pDraw[i])) {
                XDBG_RETURN_IF_FAIL(exynosLayerMngSet
                                    (pHwc->lyr_client_id, 0, 0,
                                     &pHwc->src_rect[i], &pHwc->dst_rect[i],
                                     NULL, NULL, outputlayer, pHwc->lpos[i],
                                     NULL, NULL));
            }
            else {
                XDBG_RETURN_IF_FAIL(exynosLayerMngSet
                                    (pHwc->lyr_client_id, 0, 0,
                                     &pHwc->src_rect[i], &pHwc->dst_rect[i],
                                     pHwc->pDraw[i], NULL, outputlayer,
                                     pHwc->lpos[i], NULL, NULL));
            }
        }
    }
}

void
exynosHwcSetDriFlag(DrawablePtr pDraw, Bool flag)
{
    XDBG_RETURN_IF_FAIL(pDraw != NULL);
    EXYNOSHwcPtr pHwc = EXYNOSPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;

    XDBG_RETURN_IF_FAIL(pHwc != NULL);
    used_dri *pDraw_cur = NULL, *pDraw_next = NULL;
    Bool found = FALSE;

    _exynosUsedDri2InitList(pHwc);
    xorg_list_for_each_entry_safe(pDraw_cur, pDraw_next, &pHwc->used_dri_list,
                                  used_dri_link) {
        if (pDraw_cur->pDraw == pDraw) {
            found = TRUE;
            if (flag) {
                break;
            }
            else {
                XDBG_DEBUG(MHWC,
                           "Remove dri flag for Drawable:%p, XID:%" PRIXID "\n",
                           pDraw, pDraw->id);
                xorg_list_del(&pDraw_cur->used_dri_link);
                free(pDraw_cur);
            }
        }
    }
    if (!found && flag) {
        used_dri *dri_data = calloc(1, sizeof(used_dri));

        XDBG_RETURN_IF_FAIL(dri_data != NULL);
        dri_data->pDraw = pDraw;
        xorg_list_add(&dri_data->used_dri_link, &pHwc->used_dri_list);
        XDBG_DEBUG(MHWC, "Setup dri flag for Drawable:%p, XID:%" PRIXID "\n",
                   pDraw, pDraw->id);
    }
}

Bool
exynosHwcGetDriFlag(DrawablePtr pDraw)
{
    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, FALSE);
    EXYNOSHwcPtr pHwc = EXYNOSPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;

    XDBG_RETURN_VAL_IF_FAIL(pHwc != NULL, FALSE);
    used_dri *pDraw_cur = NULL, *pDraw_next = NULL;
    Bool found = FALSE;

    _exynosUsedDri2InitList(pHwc);
    xorg_list_for_each_entry_safe(pDraw_cur, pDraw_next, &pHwc->used_dri_list,
                                  used_dri_link) {
        if (pDraw_cur->pDraw == pDraw) {
            found = TRUE;
            break;
        }
    }
    return found;
}

Bool
exynosHwcIsDrawExist(DrawablePtr pDraw)
{
    EXYNOSHwcPtr pHwc = EXYNOSPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    int j;

    for (j = 0; j < EXYNOS_HWC_MAX_LAYER; j++) {
        if (pHwc->pDraw[j] && pDraw->id == pHwc->pDraw[j]->id) {
            return TRUE;
        }
    }
    return FALSE;
}

EXYNOSLayerPos
exynosHwcGetDrawLpos(DrawablePtr pDraw)
{
    EXYNOSHwcPtr pHwc = EXYNOSPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    int i;

    for (i = 0; i < EXYNOS_HWC_MAX_LAYER; i++) {
        if (pHwc->pDraw[i] && pDraw == pHwc->pDraw[i]) {
            return pHwc->lpos[i];
        }
    }
    return LAYER_NONE;
}

Bool
exynosHwcInit(ScreenPtr pScreen)
{
    hwc_screen_info_ptr pHwcInfo = NULL;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSHwcPtr pHwc = NULL;

    pHwcInfo = calloc(1, sizeof(hwc_screen_info_rec));
    XDBG_RETURN_VAL_IF_FAIL(pHwcInfo != NULL, FALSE);

    memset(pHwcInfo, 0, sizeof(hwc_screen_info_rec));

    pHwc = calloc(1, sizeof(EXYNOSHwcRec));
    XDBG_GOTO_IF_FAIL(pHwc != NULL, fail_init);

    /* set the initial values */
    pHwc->layer_count = 0;
    pHwc->max_layers = EXYNOS_HWC_MAX_LAYER;
    // pHwc->commit_complete = TRUE; //no such field yet
    pHwc->pScreen = pScreen;
    pExynos->pHwc = pHwc;

    pHwcInfo->version = HWC_SCREEN_INFO_VERSION;
    pHwcInfo->maxLayer = EXYNOS_HWC_MAX_LAYER;
    pHwcInfo->open = EXYNOSHwcOpen;
    pHwcInfo->set_drawables = EXYNOSHwcSetDrawables;

#ifdef USE_HWC_RESIZE_MOVE
    pHwcInfo->move_drawable = EXYNOSHwcMoveDrawable;
    pHwcInfo->resize_drawable = EXYNOSHwcResizeDrawable;
#else
    pHwcInfo->move_drawable = NULL;
    pHwcInfo->resize_drawable = NULL;
#endif

    pHwcInfo->update_drawable = NULL;

    /*find what connector is active */
    int active_connector = -1;

    active_connector = findActiveConnector(pScrn);
    XDBG_GOTO_IF_FAIL(active_connector != -1, fail_init);

    if (active_connector == DRM_MODE_CONNECTOR_LVDS) {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWC] output layers on LCD.\n");
        outputlayer = LAYER_OUTPUT_LCD;
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO,
                   "[HWC] output layers on external display.\n");
        outputlayer = LAYER_OUTPUT_EXT;
    }

    pHwc->lyr_client_id = exynosLayerMngRegisterClient(pScrn, "HWC", 1);
    if (pHwc->lyr_client_id == LYR_ERROR_ID) {
        XDBG_ERROR(MHWC, "Can't register layer_id\n");
        goto fail_init;
    }
    exynosLayerMngAddEvent(pHwc->lyr_client_id, EVENT_LAYER_ANNEX,
                           _exynosLayerAnnexHandler, pHwc);
    exynosLayerMngAddEvent(pHwc->lyr_client_id, EVENT_LAYER_FREE_COUNTER,
                           _exynosLayerFreeCounterHandler, pHwc);

    pHwc->max_layers =
        exynosLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer,
                                             NULL);
    pHwcInfo->maxLayer = pHwc->max_layers;
    int i;

    for (i = 0; i < EXYNOS_HWC_MAX_LAYER; i++) {
        pHwc->lpos[i] = LAYER_NONE;
    }

    if (LoaderSymbol("hwc_screen_init")) {
        if (!hwc_screen_init(pScreen, pHwcInfo)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "[HWC] hwc_screen_init failed.\n");
            goto fail_init;
        }
    }
    else {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[HWC] hwc_screen_init not exist. XServer doesn't support HWC extension\n");
        goto fail_init;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWC] Enable HWC.\n");

    if (!_exynosOverlayRegisterEventResourceTypes()) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "[HWC] Failed to register EventResourceTypes. \n");
        return FALSE;
    }
    return TRUE;

 fail_init:
    free(pHwc);
    pExynos->pHwc = NULL;
    if (pHwcInfo)
        free(pHwcInfo);

    return FALSE;
}

EXYNOSLayerMngClientID
exynosHwcGetLyrClientId(EXYNOSHwcPtr pHwc)
{
    return pHwc->lyr_client_id;
}

void
exynosHwcDeinit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (pExynos->pHwc) {
        exynosLayerMngUnRegisterClient(pExynos->pHwc->lyr_client_id);
        free(pExynos->pHwc);
        pExynos->pHwc = NULL;
    }

    XDBG_INFO(MHWC, "Close HWC.\n");
}
