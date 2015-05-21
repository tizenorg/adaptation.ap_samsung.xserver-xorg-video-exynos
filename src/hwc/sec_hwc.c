/* sec-hwc.c
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

#include "sec.h"
#include "sec_util.h"
#include "sec_display.h"
#include <list.h>

#include "exa.h"
#include <hwc.h>
#include "sec_hwc.h"

#include "xf86drm.h"

#include "sec_layer_manager.h"

//#define USE_PIXMAN_COMPOSITE 1

#define SKIP_SET_DRAW 1

#define SEC_HWC_MAX_LAYER 4

static RESTYPE layers_rtype;
//static int active_connector = -1;

static SECLayerOutput  outputlayer = LAYER_OUTPUT_LCD; //by default LCD only
static SECLayerPos lower_layer =  LAYER_UPPER;

/* HDMI || WB || Scale disp : pHwc->gfx_max_layers = 1;
 * pHwc->gfx_max_layers = SEC_HWC_MAX_LAYER - pHwc->drv_layers; (4 -1)
 *
 * */
struct _secHwcRec
{
    ScreenPtr pScreen;

    /* number of layers used by x client */
    int gfx_layers;
    /* number of layers used by x video driver */
    int drv_layers;
    /* number of available layers for x client to use */
    int gfx_max_layers;

    SECHwcStatus hwc_status;

    Bool showComplete;

    /* requested count */
    int count;

    SECLayerPtr pLayers[SEC_HWC_MAX_LAYER];
#ifdef LAYER_MANAGER
    SECLayerMngClientID lyr_client_id;

    /* drawable id associated with this layer*/
    DrawablePtr pDraw[SEC_HWC_MAX_LAYER];
    int lpos[SEC_HWC_MAX_LAYER];
    xRectangle src_rect[SEC_HWC_MAX_LAYER];
    xRectangle dst_rect[SEC_HWC_MAX_LAYER];
    struct xorg_list used_dri_list;
#endif
};

#ifdef LAYER_MANAGER
typedef struct
{
    DrawablePtr pDraw;
    struct xorg_list used_dri_link;
}  used_dri;
#endif


static void
_hwcSaveDrawable (ScrnInfoPtr pScrn, DrawablePtr pDraw, int num, int index, int type)
{
    ScreenPtr pScreen = pDraw->pScreen;
    SECPtr pSec = SECPTR(pScrn);
    char file[128];
    char *str_type[2] = {"none", "SetDrawable"};
    PixmapPtr pPix = NULL;
    SECPixmapPriv *pExaPixPriv = NULL;

    if (!pSec->dump_info)
        return;

    XDBG_RETURN_IF_FAIL (pDraw != NULL);

    if (pDraw->type == DRAWABLE_WINDOW)
        pPix = (*pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    else
        pPix = (PixmapPtr) pDraw;

    pExaPixPriv = exaGetPixmapDriverPrivate (pPix);
    snprintf (file, sizeof(file), "[HWC]%s_%x_%p_{%d,%d}_%03d.%s",
              str_type[type],
              (unsigned int)pDraw->id,
              (void *) pPix,
              index, num,
              pExaPixPriv->dump_cnt,
              pSec->dump_type);

    secUtilDoDumpPixmaps (pSec->dump_info, pPix, file, pSec->dump_type);

    XDBG_DEBUG (MSEC, "dump done\n");
}



static inline PixmapPtr
_getPixmapFromDrawable (DrawablePtr pDraw)
{
    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, NULL);

    ScreenPtr pScreen = pDraw->pScreen;
    PixmapPtr pPix;

    if (pDraw->type == DRAWABLE_WINDOW)
        pPix = (*pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    else
        pPix = (PixmapPtr) pDraw;

    return pPix;
}
static int
_secOverlayRegisterEventDrawableGone  (void *data, XID id)
{
    SECHwcDrawableInfo * hdi = (SECHwcDrawableInfo*) data;
    XDBG_RETURN_VAL_IF_FAIL(hdi != NULL, BadValue);
    SECHwcPtr pHwc = SECPTR(hdi->pScrn)->pHwc;
    XDBG_RETURN_VAL_IF_FAIL(pHwc != NULL, BadValue);
    XDBG_WARNING(MHWC, "Drawable id[%p] has been deleted\n", id);

    int i;
    for (i = 0; i < SEC_HWC_MAX_LAYER; i++)
    {
#ifndef LAYER_MANAGER
        if (pHwc->pLayers[i] == NULL) continue;

        if (secLayerGetDraw(pHwc->pLayers[i])->id == id)
        {
            secLayerUnref(pHwc->pLayers[i]);
            pHwc->pLayers[i] = NULL;
            pHwc->count--;
            pHwc->gfx_layers--;
        }
#else
        if (pHwc->pDraw[i] != NULL)
        {
            if(pHwc->pDraw[i]->id == id)
            {
                secHwcSetDriFlag (pHwc->pDraw[i], FALSE);
                secLayerMngRelease(pHwc->lyr_client_id, outputlayer, pHwc->lpos[i]);
                pHwc->pDraw[i] = NULL;
                pHwc->lpos[i] = LAYER_NONE;
            }
        }
#endif
    }

    free(hdi);
    return Success;
}

static Bool
_secOverlayRegisterEventResourceTypes (void)
{
    layers_rtype = CreateNewResourceType (_secOverlayRegisterEventDrawableGone, "SEC Hwc Overlay Drawable");

    if (!layers_rtype)
        return FALSE;

    return TRUE;
}

/*
 * Add layer_rtype to drawable
 * When drawable is destroyed, _secOverlayRegisterEventDrawableGone
 * will be called and if that  drawable is on overlay list. it will be destroyed..
 */
Bool
_attachCallbackToDraw(DrawablePtr pDraw, SECLayerPtr pLayer, ScrnInfoPtr pScrn)
{
    SECHwcDrawableInfo* resource;
    resource = malloc(sizeof(SECHwcDrawableInfo));
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

void
_reorderLayers(SECHwcPtr pHwc, DrawablePtr *pDraws, xRectangle *srcRects, xRectangle *dstRects, int count)
{
    SECLayer* pOldLayers[SEC_HWC_MAX_LAYER] = { 0, };

    int i, j;

    if (pHwc->pLayers[0] != 0)
    {
        /* save the previous overlays and set lpos to LAYER_NONE*/
        for (i = 0; i < pHwc->gfx_layers; i++) {
            pOldLayers[i] = pHwc->pLayers[i];
            pHwc->pLayers[i] = NULL;
        }

        /* re-order the overlays */
        for (i = 0; i < count; i++)
        {
            for (j = 0; j < pHwc->gfx_layers; j++)
            {
                if (pOldLayers[j] == NULL) continue;
                if (pDraws[i]->id == secLayerGetDraw(pOldLayers[j])->id)
                {
                    if (i != j)
                    {
                        secLayerFreezeUpdate(pOldLayers[i], TRUE);
                        secLayerSetPos(pOldLayers[i], LAYER_NONE);
                    }
                    pHwc->pLayers[i] = pOldLayers[j];
                    pOldLayers[j] = NULL;
                    break;
                }
            }
        }

        /* destroy the unused layers */
        for (i = 0; i < pHwc->gfx_layers; i++) {
            if (pOldLayers[i])
                secLayerUnref(pOldLayers[i]);
        }

    }
    else
    {
        /* FIXME:: */
        pHwc->pLayers[0] = secLayerGetDefault(NULL);
    }
}

/*
 * The first drawable passing through SECHwcSetDrawables should be placed into top layer.
 * In order that the lower layers were visible the user has to take care independently of that
 * the area over the lower layer would be transparent. It to get capability
 * to hide a layer without calling of SECHwcSetDrawables and to avoid a blinking.
 */
static Bool
_secHwcDoSetDrawables (SECHwcPtr pHwc, DrawablePtr *pDraws, xRectangle *srcRects, xRectangle *dstRects, int count)
{
    ScreenPtr pScreen = pHwc->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
#ifndef LAYER_MANAGER
    int lpos = LAYER_UPPER;
#else
    int lpos[SEC_HWC_MAX_LAYER] = {0};
    int max_lpos;
#endif
    int i;

    /* TODO: check the previous set_drawable is completed and wait if no*/
    if (!pHwc->showComplete)
    {
    }

#ifndef LAYER_MANAGER
    _reorderLayers(pHwc, pDraws, srcRects, dstRects, count);
#else
    max_lpos = secLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer, lpos);
    if (max_lpos < count)
    {
        XDBG_ERROR(MHWC, "Count of drawables:%d more of available planes:%d\n", count, max_lpos);
        return FALSE;
    }
    pHwc->gfx_max_layers = max_lpos;

    //set a tracking function to the new drawable
    for (i = 0; i < count; i++)
    {
        if (!secHwcIsDrawExist(pDraws[i]))
            _attachCallbackToDraw (pDraws[i], NULL, pScrn);
    }
#endif

    /* reassign count to currently available number of layers*/
    for (i = 0; i < count; i++)
    {
#ifndef LAYER_MANAGER
        if (pHwc->pLayers[i] ==  NULL)
        {
            pHwc->pLayers[i] = secLayerCreate(pScrn, outputlayer, lpos);
            XDBG_GOTO_IF_FAIL(pHwc->pLayers[i] != NULL, fail_setdrwbs);
            _attachCallbackToDraw( pDraws[i], pHwc->pLayers[i], pScrn);
        }

        secLayerFreezeUpdate(pHwc->pLayers[i], TRUE);
        secLayerEnableVBlank(pHwc->pLayers[i], TRUE);

        XDBG_GOTO_IF_FAIL( secLayerSetOffset(pHwc->pLayers[i], 0, 0),               fail_setdrwbs);
        XDBG_GOTO_IF_FAIL( secLayerSetRect(pHwc->pLayers[i], &srcRects[i], &dstRects[i]), fail_setdrwbs);
        XDBG_GOTO_IF_FAIL( secLayerSetPos(pHwc->pLayers[i], lpos),                  fail_setdrwbs);

        secLayerFreezeUpdate(pHwc->pLayers[i], FALSE);
        if (!secLayerIsUpdateDRI(pHwc->pLayers[i]))
            XDBG_GOTO_IF_FAIL( secLayerSetDraw(pHwc->pLayers[i], pDraws[i]),  fail_setdrwbs);

        if (!secLayerIsVisible (pHwc->pLayers[i]))
        {
            secLayerShow(pHwc->pLayers[i]);
        }
        else if (secLayerIsNeedUpdate(pHwc->pLayers[i]))
        {
            if (!secLayerIsUpdateDRI(pHwc->pLayers[i]))
                secLayerUpdate(pHwc->pLayers[i]);
        }
        --lpos;
#else
        if (pDraws[i] && pHwc->pDraw[i] && pDraws[i]->id != pHwc->pDraw[i]->id)
        {
            secLayerMngClearQueue(pHwc->lyr_client_id, outputlayer, pHwc->lpos[i]);
            secHwcSetDriFlag (pHwc->pDraw[i], FALSE);
            secHwcSetDriFlag (pDraws[i], FALSE);
        }
        if (secHwcGetDriFlag (pDraws[i]))
        {
            XDBG_GOTO_IF_FAIL(secLayerMngSet(pHwc->lyr_client_id, 0, 0, &srcRects[i], &dstRects[i],
                                             NULL, NULL, outputlayer, lpos[(max_lpos-1) - i], NULL, NULL), fail_setdrwbs);
        }
        else
        {
            XDBG_GOTO_IF_FAIL(secLayerMngSet(pHwc->lyr_client_id, 0, 0, &srcRects[i], &dstRects[i],
                                             pDraws[i], NULL, outputlayer, lpos[(max_lpos-1) - i], NULL, NULL), fail_setdrwbs);
        }
        pHwc->pDraw[i] = pDraws[i];
        pHwc->lpos[i] = lpos[(max_lpos-1) - i];
        pHwc->src_rect[i] = srcRects[i];
        pHwc->dst_rect[i] = dstRects[i];

#endif
     }

    /* Set the default layer at LCD.
       We assumes that the last one is the default one */
#ifndef LAYER_MANAGER
    secLayerSetAsDefault(pHwc->pLayers[0]);
#endif

#ifdef LAYER_MANAGER
    Bool def_layer_setup = FALSE;
    for (i = 0 ; i < count; i++)
    {
        if (pHwc->lpos[i] && pHwc->lpos[i]== LAYER_DEFAULT)
        {
            def_layer_setup = TRUE;
            break;
        }
    }
    if (!def_layer_setup)
    {
        for (i = 0 ; i < max_lpos; i++)
            if (lpos[i] == LAYER_DEFAULT)
            {

            }
    }
    /* Remove old */
    for (i = count; i < SEC_HWC_MAX_LAYER; i++)
    {
        if (pHwc->pDraw[i])
        {
            secHwcSetDriFlag (pHwc->pDraw[i], FALSE);
            secLayerMngRelease(pHwc->lyr_client_id, outputlayer, pHwc->lpos[i]);
            pHwc->lpos[i] = LAYER_NONE;
            pHwc->pDraw[i] = NULL;
        }
    }
#endif

    /* set count of gfx_layers to the hwc */
    pHwc->gfx_layers = count;

    return TRUE;

fail_setdrwbs:
    XDBG_ERROR(MHWC, "HWC set drawables failed. Let's update ScreenPixmap. gfx_layers=%d\n", pHwc->gfx_layers);

    /* Before reset the Overlay list, we need to update screen pixmap! */
#ifndef LAYER_MANAGER
    secHwcGetScreenPixmap(pScreen);
#endif

    for (i = 0; i < count; i++)
    {
#ifndef LAYER_MANAGER
        secLayerUnref(pHwc->pLayers[i]);
#else
        secLayerMngRelease(pHwc->lyr_client_id, outputlayer, lpos[(max_lpos-1) - i]);
#endif
        pHwc->pLayers[i] = NULL;
    }

    // TODO: set the original values
    /* set count of gfx_layers to the hwc */
#ifndef LAYER_MANAGER
    pHwc->gfx_max_layers = SEC_HWC_MAX_LAYER - pHwc->drv_layers;
#endif
    return FALSE;
}

//void
//secHwcUpdate(DrawablePtr pDraw, Bool enable)
//{
//#ifndef LAYER_MANAGER
//#else
//    SECHwcPtr pHwc = SECPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
//    int j;
//    for (j = 0; j < SEC_HWC_MAX_LAYER; j++)
//    {
//        if (pHwc->pDraw[j] && pDraw == pHwc->pDraw[j])
//        {
//            pHwc->update[j] = enable;
//        }
//    }
//#endif
//}


Bool
secHwcDoSetRootDrawables (SECHwcPtr pHwc, PixmapPtr pScreenPixmap)
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

    ret = _secHwcDoSetDrawables(pHwc, (DrawablePtr * )&pScreenPixmap, &dstRect, &srcRect, 1);

    return ret;

}

PixmapPtr
secHwcGetScreenPixmap (ScreenPtr pScreen)
{
#ifdef USE_PIXMAN_COMPOSITE
    XDBG_DEBUG(MHWC,"================> merge layers to pixmap of root window.  \n");

    SECPtr pSec = SECPTR(xf86ScreenToScrn(pScreen));
    SECHwcPtr pHwc = pSec->pHwc;

    pixman_image_t *pSrcImg = NULL;
    pixman_image_t *pDstImg = NULL;

    PixmapPtr       pDstPixmap = NULL;
    PixmapPtr       pSrcPixmap = NULL;
    DrawablePtr     pSrcDraw = NULL;

    SECPixmapPriv  *pSrcPixmapPriv = NULL;
    SECPixmapPriv  *pDstPixmapPriv = NULL;

    uint32_t *      pSrcBits = NULL;
    uint32_t *      pDstbits = NULL;

    Bool need_finish = FALSE;

    int i=0;
    int pixman_op = PIXMAN_OP_SRC;

    /*
    * Step 1. Initialize dest pixmap private data.
    *             - It will be a root screen pixmap.
    */
    pDstPixmap = (*pScreen->GetScreenPixmap) (pScreen);
    pDstPixmapPriv = exaGetPixmapDriverPrivate(pDstPixmap);
    XDBG_GOTO_IF_FAIL(pDstPixmapPriv != NULL, fail_mergesrn);
    if (!pDstPixmapPriv->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pDstPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL (pDstPixmapPriv->bo != NULL, fail_mergesrn);
    }

    pDstbits = tbm_bo_map(pDstPixmapPriv->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE).ptr;
    pDstImg = pixman_image_create_bits(PIXMAN_a8r8g8b8, pDstPixmap->drawable.width, pDstPixmap->drawable.height,
                                                          pDstbits, pDstPixmap->devKind);

    /*
    * Step 2. Recursive composite between hwc layer's pixmap and root screen pixmap
    *             - The result is stored in root screen pixmap (dest pixmap).
    */
    for (i = 0; i < pHwc->gfx_layers; i++)
    {
#ifndef LAYER_MANAGER
        pSrcDraw = secLayerGetDraw (pHwc->pLayers[i]);
        XDBG_GOTO_IF_FAIL(pSrcDraw != NULL, fail_mergesrn);

        pSrcPixmap = _getPixmapFromDrawable (secLayerGetDraw (pHwc->pLayers[i]));
        XDBG_GOTO_IF_FAIL(pSrcPixmap != NULL, fail_mergesrn);

#else
        if (!pHwc->pDraw[i] || pHwc->dst_rect[i].width == 0 || pHwc->dst_rect[i].height == 0)
        {
            continue;
        }
        pSrcDraw = pHwc->pDraw[i];
        XDBG_GOTO_IF_FAIL(pSrcDraw != NULL, fail_mergesrn);
        pSrcPixmap = _getPixmapFromDrawable (pHwc->pDraw[i]);
        XDBG_GOTO_IF_FAIL(pSrcPixmap != NULL, fail_mergesrn);
#endif
        pSrcPixmapPriv = exaGetPixmapDriverPrivate (pSrcPixmap);
        if (pDstPixmap == pSrcPixmap)
            continue;
        XDBG_GOTO_IF_FAIL(pSrcPixmapPriv != NULL, fail_mergesrn);
        XDBG_GOTO_IF_FAIL(pSrcPixmapPriv->bo != NULL, fail_mergesrn);

        pSrcBits = tbm_bo_map (pSrcPixmapPriv->bo, TBM_DEVICE_CPU, TBM_OPTION_READ).ptr;
        pSrcImg = pixman_image_create_bits (PIXMAN_a8r8g8b8, pSrcPixmap->drawable.width, pSrcPixmap->drawable.height,
                                            pSrcBits, pSrcPixmap->devKind);
        XDBG_GOTO_IF_FAIL(pSrcImg != NULL, fail_mergesrn);

        /*
         * for base layer(first layer) PIXMAN_OP_SRC should be used
         */
        if (pSrcDraw->depth == 32 && i != 0)
            pixman_op = PIXMAN_OP_OVER;
        else
            pixman_op = PIXMAN_OP_SRC;

        XDBG_DEBUG (MHWC, "pixmap operation(%d): layer_idx=%d [%d,%d %dx%d] => [%d,%d %dx%d]\n", pixman_op, i, 0, 0,
                    pSrcPixmap->drawable.width, pSrcPixmap->drawable.height, pSrcPixmap->drawable.x,
                    pSrcPixmap->drawable.y, pDstPixmap->drawable.width, pDstPixmap->drawable.height);

        xRectangle destRect = {0, };
#ifndef LAYER_MANAGER
        secLayerGetRect(pHwc->pLayers[i], NULL, &destRect);
#else
        memcpy(&destRect, &pHwc->dst_rect[i], sizeof(xRectangle));
#endif

        pixman_image_composite (pixman_op, pSrcImg, NULL, pDstImg, 0, 0, 0, 0, pSrcPixmap->screen_x + destRect.x,
                                pSrcPixmap->screen_y + destRect.y, pSrcPixmap->drawable.width, pSrcPixmap->drawable.height);
        if (pSrcBits != NULL)
        {
            tbm_bo_unmap (pSrcPixmapPriv->bo);
            pSrcBits = NULL;
        }
    }

    XDBG_DEBUG (MHWC, "pixmap composite is done!!!!!!!");

fail_mergesrn:

    if (pDstPixmap && need_finish)
        secExaFinishAccess (pDstPixmap, EXA_PREPARE_DEST);

    if( pSrcImg )
    {
        pixman_image_unref(pSrcImg);
        pSrcImg = NULL;
    }

    if( pDstbits != NULL )
    {
        tbm_bo_unmap(pDstPixmapPriv->bo);
        pDstbits = NULL;
    }

    if( pDstImg )
    {
        pixman_image_unref(pDstImg);
        pDstImg = NULL;
    }

    return pDstPixmap;
#else
    return (*pScreen->GetScreenPixmap) (pScreen);
#endif
}

static Bool
_secHwcSetValid (DrawablePtr *pDraws, xRectangle *srcRects, xRectangle *dstRects, int count)
{
    DrawablePtr pDraw = NULL;
    xRectangle srcRect, dstRect;
    int i;

    for (i = 0; i < count; i++)
    {
        pDraw = pDraws[i];
        srcRect = srcRects[i];
        dstRect = dstRects[i];
        XDBG_GOTO_IF_FAIL (pDraw->x >= 0, fail);
        XDBG_GOTO_IF_FAIL (pDraw->y >= 0, fail);
        XDBG_GOTO_IF_FAIL (pDraw->width > 0, fail);
        XDBG_GOTO_IF_FAIL (pDraw->height > 0, fail);

        XDBG_GOTO_IF_FAIL (srcRect.x >= 0, fail);
        XDBG_GOTO_IF_FAIL (srcRect.y >= 0, fail);
        XDBG_GOTO_IF_FAIL (srcRect.width > 0, fail);
        XDBG_GOTO_IF_FAIL (srcRect.height > 0, fail);
        XDBG_GOTO_IF_FAIL (dstRect.x >= 0, fail);
        XDBG_GOTO_IF_FAIL (dstRect.y >= 0, fail);
        XDBG_GOTO_IF_FAIL (dstRect.width > 0, fail);
        XDBG_GOTO_IF_FAIL (dstRect.height > 0, fail);

        XDBG_DEBUG (MHWC, "xid=%p pDraw(x,y,w,h)=(%d,%d,%d,%d) pSrcRect(%d,%d,%d,%d) pDstRect(%d,%d,%d,%d)\n",
            (void *)pDraw->id, pDraw->x, pDraw->y, pDraw->width, pDraw->height,
            srcRect.x, srcRect.y, srcRect.width, srcRect.height,
            dstRect.x, dstRect.y, dstRect.width, dstRect.height);
    }

    return TRUE;
fail:
    XDBG_ERROR (MHWC, "Error: Drawable is not valid. ==> xid=%p pDraw(x,y,w,h)=(%d,%d,%d,%d) pSrcRect(%d,%d,%d,%d) pDstRect(%d,%d,%d,%d)\n",
               (void *)pDraw->id, pDraw->x, pDraw->y, pDraw->width, pDraw->height,
               srcRect.x, srcRect.y, srcRect.width, srcRect.height,
               dstRect.x, dstRect.y, dstRect.width, dstRect.height);

    return FALSE;
}

/* seems to me this handler will be necessary, but don't know how at this moment) */
//static void
//SECHwcSetDrawableCommitHandler (unsigned int frame, unsigned int tv_sec,
//                         unsigned int tv_usec, void *event_data)
//{
//    XDBG_DEBUG (MHWC, "========= COMMITTED.\n");
//
//    SECHwcPtr pHwc = (SECHwcPtr) event_data;
//
//    pHwc->showComplete = TRUE;
//}

//static void
//SECHwcUpdateEventHandler (unsigned int frame, unsigned int tv_sec,
//                         unsigned int tv_usec, void *event_data)
//{
//    XDBG_DEBUG (MHWC, "\n");
//}
//

#ifdef USE_HWC_RESIZE_MOVE
static DrawablePtr
_secHwcGetClientValidDraw (SECHwcPtr pHwc, DrawablePtr pDraw)
{
    //WindowPtr pWin, pChildWin, pClientWin;
    WindowPtr pWin;
    DrawablePtr pClientDraw = NULL;
    int i;

    if (pDraw->type != DRAWABLE_WINDOW)
        return NULL;

    pWin = (WindowPtr)pDraw;

/* in our case firstChild  and lastChild always 0
    we need another method for analyze */
/*
    pChildWin = (WindowPtr)(pWin->firstChild);
    if (!pChildWin)
        return NULL;

    do {
        if (pChildWin->firstChild)
        {
            pClientWin = pChildWin->firstChild;
            for (i = 0; i < pHwc->gfx_layers; i++)
            {
                if (secLayerGetDraw(pHwc->pLayers[i])->id == pClientWin->drawable.id)
                {
                    pClientDraw = &pClientWin->drawable;
                    break;
                }
            }
            if (pClientDraw)
                break;
        }
        pChildWin = pChildWin->nextSib;
    } while (pChildWin);
*/
   // pChildWin = (WindowPtr)(pWin->firstChild);
    if (!pWin)
        return NULL;


    for (i = 0; i < pHwc->gfx_layers; i++)
    {
        if (secLayerGetDraw (pHwc->pLayers [i])->id == pWin->drawable.id)
        {
            pClientDraw = &pWin->drawable;
            return pClientDraw;
        }

    }

    return NULL;
}

static void
SECHwcMoveDrawable (ScreenPtr pScreen, DrawablePtr pDraw, int x, int y)
{
    XDBG_DEBUG (MHWC, "Move drawable\n");

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = pSec->pHwc;
    xRectangle srcrect, dstrect;

    SECLayer* pLayer = NULL;
    DrawablePtr pClientDraw = NULL;

    int i;

    /* debug */
#if 0
    XDBG_INFO  (MHWC, "Requested Move (%d,%d), Window(id:%p)(%d,%d)\n",
            x, y, (void *)pDraw->id, pDraw->x, pDraw->y);
#endif

    /* Get the client's drawable */
    pClientDraw = _secHwcGetClientValidDraw (pHwc, pDraw);
    if (!pClientDraw)
        return;

    /* Get the layer associated with the client's drawable */
    for (i = 0; i < pHwc->gfx_layers; i++)
    {
        if (secLayerGetDraw (pHwc->pLayers[i])->id == pClientDraw->id)
        {
            pLayer = pHwc->pLayers[i];
            break;
        }
    }
    if (pLayer == NULL)
    {
        XDBG_WARNING (MHWC, "pLayer not initialized!\n");
        return;
    }

    secLayerGetRect (pLayer, &srcrect, &dstrect);

    /* update the drawable only when the position is changed. */
    if (x != srcrect.x || y != srcrect.y)
    {
        XDBG_DEBUG (MHWC, "Move :: (x,y)=(%d,%d)\n", x, y);
        XDBG_DEBUG (MHWC, "   Border(id:%p) (%d,%d), Client(id:%p) (%d,%d)\n",
                   (void *)pDraw->id, pDraw->x, pDraw->y,
                   (void *)pClientDraw->id, pClientDraw->x, pClientDraw->y);

        dstrect = srcrect;
        dstrect.x = x;
        dstrect.y = y;

        secLayerFreezeUpdate (pLayer, TRUE);

        if (!secLayerSetOffset (pLayer, 0, 0))
        {
            XDBG_WARNING (MHWC, "SECHwcMoveDrawable set layer offset failed!.\n");
            return;
        }

        if (!secLayerSetRect (pLayer, &srcrect, &dstrect))
        {
            XDBG_WARNING (MHWC, "SECHwcMoveDrawable set layer rect failed!.\n");
            return;

        }
        /* set layers according to the drawables' info */
        secLayerShow (pLayer);

        /// TODO
        /* Check that display is OFF*/
    }
}

static void
SECHwcResizeDrawable (ScreenPtr pScreen, DrawablePtr pDraw, int x, int y, int w, int h)
{
    /* Resize window now only works properly to decrease direction(window become smaller),
     * when w and h < srcrect.width and srcrect.height
     * In other case it can cause an error
     *
     * */
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = pSec->pHwc;
    xRectangle srcrect, dstrect;

    SECLayer* pLayer = NULL;
    DrawablePtr pClientDraw = NULL;

    int i;

/* debug */
#if 0
    XDBG_INFO (MHWC, "Requested Resize (%d,%d), Window(id:%p)(%d,%d,%d,%d)\n",
        x, y, (void *)pDraw->id, x, y, w, h);
#endif

    /* Get the client's drawable */
    pClientDraw = _secHwcGetClientValidDraw (pHwc, pDraw);
    if (!pClientDraw)
        return;

    /* Get the layer associated with the client's drawable */
    for (i = 0; i < pHwc->gfx_layers; i++)
    {
        if (secLayerGetDraw (pHwc->pLayers[i])->id == pClientDraw->id)
        {
            pLayer = pHwc->pLayers[i];
            break;
        }
    }
    /* check the region of client drawable and the region of requested region.
     * The client window does not update with the requested configurewindow.
     * Border window and Client window is different (geometry is different).
     */
//    if (x != pClientDraw->x ||
//        y != pClientDraw->y ||
//        w != pClientDraw->width ||
//        h != pClientDraw->height)
//    {
//#if SKIP_SET_DRAW
//       // TODO: this skip update will be removed when the rotation issue on illume is active.
//        /* skip the next set drawable
//           because border is changed but client window is not changed yet. */
//       // pLayer->skip_update = TRUE;
//#endif
//
//        XDBG_DEBUG (MHWC, "set the skip set_drawable.(id:%p, pLayer:%p\n",
//            (void *)pClientDraw->id, pLayer);
//        return;
//    }



    if (pLayer == NULL)
    {
        XDBG_WARNING (MHWC, "pLayer not initialized!\n");
        return;
    }

    secLayerGetRect (pLayer, &srcrect, &dstrect);
    /* update the drawable only when the position is changed. */
    if (x != srcrect.x || y != srcrect.y || w != srcrect.width || h != srcrect.height)
    {
        XDBG_DEBUG (MHWC, "Resize :: (x,y,w,h)=(%d,%d,%d,%d)\n", x, y, w, h);
        XDBG_DEBUG (MHWC, "   Border(id:%p) (%d,%d,%d,%d), Client(id:%p) (%d,%d,%d,%d)\n", (void * )pDraw->id, pDraw->x,
                    pDraw->y, pDraw->width, pDraw->height, (void * )pClientDraw->id, pClientDraw->x, pClientDraw->y,
                    pClientDraw->width, pClientDraw->height);

        dstrect.x = x;
        dstrect.y = y;
        dstrect.width = w;
        dstrect.height = h;

        secLayerFreezeUpdate (pLayer, TRUE);

        if (!secLayerSetOffset (pLayer, 0, 0))
        {
            XDBG_WARNING (MHWC, "SECHwcMoveDrawable set layer offset failed!.\n");
            return;

        }
        if (!secLayerSetRect (pLayer, &srcrect, &dstrect))
        {
            XDBG_WARNING (MHWC, "SECHwcMoveDrawable set layer rect failed!.\n");
            return;

        }
        /* set layers according to the drawables' info */
        secLayerShow (pLayer);

        /// TODO
        /* Check that display is OFF*/
    }
}
#endif

static int
SECHwcOpen (ScreenPtr pScreen, int *maxLayer)
{
    XDBG_DEBUG(MHWC, "enter! \n");

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = pSec->pHwc;
    *maxLayer = pHwc->gfx_max_layers;
    return Success;
}

int
SECHwcSetDrawables (ScreenPtr pScreen, DrawablePtr *pDraws, xRectangle *srcRects, xRectangle *dstRects, int count)
{
    XDBG_DEBUG(MHWC, "================> requested set_drawables. # of drawables are %d \n", count);

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = pSec->pHwc;
    int i;
#ifdef LAYER_MANAGER
    if (count == 0)
    {
        SECPTR(pScrn)->hwc_active = TRUE;
//        SECPTR(pScrn)->hwc_use_def_layer = TRUE;
//        for (i = 0 ; i < pHwc->gfx_layers; i++)
//        {
//            if (pHwc->pDraw[i])
//            {
//                secHwcSetDriFlag (pHwc->pDraw[i], FALSE);
//                secLayerMngRelease(pHwc->lyr_client_id, outputlayer, pHwc->lpos[i]);
//                pHwc->lpos[i] = LAYER_NONE;
//                pHwc->pDraw[i] = NULL;
//            }
//        }
        PixmapPtr pScreenPixmap = NULL;
        /*merge all layers to root*/
        pScreenPixmap = secHwcGetScreenPixmap(pScreen);
        if (pScreenPixmap == NULL)
        {
            XDBG_ERROR(MHWC, "secHwcMergeScreenPixmap failed.\n");
            return BadMatch;
        }

        if(!secHwcDoSetRootDrawables(pHwc, pScreenPixmap))
        {
            XDBG_WARNING (MHWC, "secHwcDoSetRootDrawables failed.\n");
            return BadMatch;
        }
        return Success;
    }
#endif

    XDBG_RETURN_VAL_IF_FAIL(pDraws != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(srcRects != NULL, FALSE);
    //XDBG_RETURN_VAL_IF_FAIL (dstRects != NULL, FALSE);

//    if ((secDispHdmiEnabled (pDisp) || secDispWbEnabled (pDisp)) && count >= 2)
//    {
//        XDBG_ERROR (MHWC, "pHwc->gfx_max_layers(%d) count(%d) ==> wfd or hdmi is enabled. count should be one!!\n",
//            pHwc->gfx_max_layers, count);
//    }

    if (!_secHwcSetValid (pDraws, srcRects, dstRects, count))
        return BadMatch;

    if ((pHwc->drv_layers + count) > SEC_HWC_MAX_LAYER)
    {
        XDBG_ERROR(MHWC, "Error:: drv_layers(%d) and count(%d) is more than MAX LAYERS(%d).\n",
                pHwc->gfx_max_layers, count, SEC_HWC_MAX_LAYER);
        return BadMatch;
    }


    SECPTR(pScrn)->hwc_active = TRUE;
    SECPTR(pScrn)->hwc_use_def_layer = FALSE;
    if (count == 1
            && (srcRects[0].width > pDraws[0]->width || srcRects[0].height > pDraws[0]->height))
    {
        XDBG_WARNING(MHWC, "XID=%#x SRC rect=%d,%d-%dx%d is bigger than SRC GEOM=%d,%d-%dx%d\n",
                     pDraws[0]->id, srcRects[0].x, srcRects[0].y,
                srcRects[0].width, srcRects[0].height, pDraws[0]->x,
                pDraws[0]->y, pDraws[0]->width, pDraws[0]->height);
        return BadMatch;
    }

    if (!_secHwcDoSetDrawables(pHwc, pDraws, srcRects, dstRects, count))
    {
        XDBG_WARNING(MHWC, "_secHwcDoSetDrawables failed.\n");
        return BadMatch;
    }


    pHwc->gfx_layers = count;

    for (i = 0; i < count; i++)
        _hwcSaveDrawable (pScrn, pDraws[i], count, i, 1);


    XDBG_DEBUG(MHWC, "================> end set_drawable gfx_layers=%d, drv_layers=%d, max_gfx_layers=%d\n",
            pHwc->gfx_layers, pHwc->drv_layers, pHwc->gfx_max_layers);

    return Success;
}


Bool
SECUpdateHwcOverlays ( SECModePtr  pDisp /*SECDispInfoPtr pDisp*/, SECHwcPtr pHwc)
{
    XDBG_DEBUG(MHWC, " Update hw_overlays \n");
    return TRUE;
}
#ifdef LAYER_MANAGER
static void
_secLayerAnnexHandler (void* user_data, SECLayerMngEventCallbackDataPtr data)
{
    SECHwcPtr pHwc = (SECHwcPtr)user_data;
    int new_gfx_max_layers = secLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer, NULL);
    int old_gfx_max_layers = pHwc->gfx_max_layers;
    if (pHwc->gfx_layers <= new_gfx_max_layers)
    {
        _secHwcDoSetDrawables (pHwc, pHwc->pDraw, pHwc->src_rect, pHwc->dst_rect, pHwc->gfx_layers);
    }
    /* send configure notify */
    if (new_gfx_max_layers != old_gfx_max_layers)
    {
        pHwc->gfx_max_layers = new_gfx_max_layers;
        hwc_send_config_notify (pHwc->pScreen, pHwc->gfx_max_layers);
        XDBG_DEBUG (MHWC, "hwc_send_config_notify - pHwc->gfx_max_layers(%d)\n",pHwc->gfx_max_layers);
    }
}

static void
_secLayerFreeCounterHandler (void* user_data, SECLayerMngEventCallbackDataPtr data)
{
    SECHwcPtr pHwc = (SECHwcPtr)user_data;
    int new_gfx_max_layers = secLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer, NULL);
    if (pHwc->gfx_max_layers != new_gfx_max_layers)
    {
        pHwc->gfx_max_layers = new_gfx_max_layers;
        /* send configure notify */
        hwc_send_config_notify (pHwc->pScreen, pHwc->gfx_max_layers);
        XDBG_DEBUG (MHWC, "hwc_send_config_notify - pHwc->gfx_max_layers(%d)\n",pHwc->gfx_max_layers);
    }
}
#endif
#ifdef LAYER_MANAGER
static void
_secUsedDri2InitList (SECHwcPtr pHwc)
{
    static Bool dri_list_init = FALSE;
    if (!dri_list_init)
    {
        xorg_list_init (&pHwc->used_dri_list);
        dri_list_init = TRUE;
    }
}
void
secHwcUpdate (ScrnInfoPtr pScrn)
{
#ifdef LAYER_MANAGER
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = pSec->pHwc;
    int i;
    for (i = 0; i < SEC_HWC_MAX_LAYER; i++)
    {
        if (pHwc->pDraw[i])
        {
            if (secHwcGetDriFlag (pHwc->pDraw[i]))
            {
                XDBG_RETURN_IF_FAIL(secLayerMngSet(pHwc->lyr_client_id, 0, 0, &pHwc->src_rect[i], &pHwc->dst_rect[i],
                                                   NULL, NULL, outputlayer, pHwc->lpos[i], NULL, NULL));
            }
            else
            {
                XDBG_RETURN_IF_FAIL(secLayerMngSet(pHwc->lyr_client_id, 0, 0, &pHwc->src_rect[i], &pHwc->dst_rect[i],
                                                   pHwc->pDraw[i], NULL, outputlayer, pHwc->lpos[i], NULL, NULL));
            }
        }
    }
#endif
}

void
secHwcSetDriFlag (DrawablePtr pDraw, Bool flag)
{
    XDBG_RETURN_IF_FAIL(pDraw != NULL);
    SECHwcPtr pHwc = SECPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    XDBG_RETURN_IF_FAIL(pHwc != NULL);
    used_dri *pDraw_cur = NULL, *pDraw_next = NULL;
    Bool found = FALSE;
    _secUsedDri2InitList (pHwc);
    xorg_list_for_each_entry_safe (pDraw_cur, pDraw_next, &pHwc->used_dri_list, used_dri_link)
    {
        if (pDraw_cur->pDraw == pDraw)
        {
            found = TRUE;
            if (flag)
            {
                break;
            }
            else
            {
                XDBG_DEBUG(MHWC, "Remove dri flag for Drawable:%p, XID:%"PRIXID"\n", pDraw, pDraw->id);
                xorg_list_del(&pDraw_cur->used_dri_link);
                free(pDraw_cur);
            }
        }
    }
    if (!found && flag)
    {
        used_dri *dri_data = calloc(1, sizeof(used_dri));
        XDBG_RETURN_IF_FAIL(dri_data != NULL);
        dri_data->pDraw = pDraw;
        xorg_list_add(&dri_data->used_dri_link, &pHwc->used_dri_list);
        XDBG_DEBUG(MHWC, "Setup dri flag for Drawable:%p, XID:%"PRIXID"\n", pDraw, pDraw->id);
    }
}

Bool
secHwcGetDriFlag (DrawablePtr pDraw)
{
    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, FALSE);
    SECHwcPtr pHwc = SECPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    XDBG_RETURN_VAL_IF_FAIL(pHwc != NULL, FALSE);
    used_dri *pDraw_cur = NULL, *pDraw_next = NULL;
    Bool found = FALSE;
    _secUsedDri2InitList (pHwc);
    xorg_list_for_each_entry_safe (pDraw_cur, pDraw_next, &pHwc->used_dri_list, used_dri_link)
    {
        if (pDraw_cur->pDraw == pDraw)
        {
            found = TRUE;
            break;
        }
    }
    return found;
}
#endif
Bool
secHwcIsDrawExist (DrawablePtr pDraw)
{
#ifdef LAYER_MANAGER
    SECHwcPtr pHwc = SECPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    int j;
    for (j = 0; j < SEC_HWC_MAX_LAYER; j++)
    {
        if (pHwc->pDraw[j] && pDraw->id == pHwc->pDraw[j]->id)
        {
            return TRUE;
        }
    }
    return FALSE;
#else
    return secLayerFindByDraw (pDraw) ? TRUE : FALSE;
#endif
}

SECLayerPos
secHwcGetDrawLpos (DrawablePtr pDraw)
{
#ifdef LAYER_MANAGER
    SECHwcPtr pHwc = SECPTR(xf86Screens[pDraw->pScreen->myNum])->pHwc;
    int i;
    for (i = 0; i < SEC_HWC_MAX_LAYER; i++)
    {
        if (pHwc->pDraw[i] && pDraw == pHwc->pDraw[i])
        {
            return pHwc->lpos[i];
        }
    }
#endif
    return LAYER_NONE;
}

Bool
secHwcInit (ScreenPtr pScreen)
{
    hwc_screen_info_ptr pHwcInfo = NULL;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);
    SECHwcPtr pHwc = NULL;

    pHwcInfo = calloc(1, sizeof(hwc_screen_info_rec));
    XDBG_RETURN_VAL_IF_FAIL(pHwcInfo != NULL, FALSE);

    memset(pHwcInfo, 0, sizeof(hwc_screen_info_rec));

    pHwc = calloc(1, sizeof(SECHwcRec));
    XDBG_GOTO_IF_FAIL(pHwc != NULL, fail_init);

    /* set the initial values */
    pHwc->gfx_layers = 0;
    pHwc->drv_layers = 0;
    pHwc->gfx_max_layers = SEC_HWC_MAX_LAYER;
    // pHwc->commit_complete = TRUE; //no such field yet
    pHwc->pScreen = pScreen;
    pSec->pHwc = pHwc;

    pHwcInfo->version = HWC_SCREEN_INFO_VERSION;
    pHwcInfo->maxLayer = SEC_HWC_MAX_LAYER;
    pHwcInfo->open = SECHwcOpen;
    pHwcInfo->set_drawables = SECHwcSetDrawables;

#ifdef USE_HWC_RESIZE_MOVE
    pHwcInfo->move_drawable = SECHwcMoveDrawable;
    pHwcInfo->resize_drawable = SECHwcResizeDrawable;
#else
    pHwcInfo->move_drawable = NULL;
    pHwcInfo->resize_drawable = NULL;
#endif

#if 0
    pHwcInfo->update_drawable = SECHwcUpdateDrawable;
#else
    pHwcInfo->update_drawable = NULL;
#endif

    /*find what connector is active*/
    int active_connector = -1;
    active_connector = findActiveConnector(pScrn);
    XDBG_GOTO_IF_FAIL(active_connector != -1, fail_init);

    if (active_connector == DRM_MODE_CONNECTOR_LVDS)
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWC] output layers on LCD.\n");
        outputlayer = LAYER_OUTPUT_LCD;
        lower_layer = LAYER_LOWER2;
    }
    else
    {
        xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWC] output layers on external display.\n");
        outputlayer = LAYER_OUTPUT_EXT;
        lower_layer = LAYER_LOWER1;
    }
#ifdef LAYER_MANAGER
    pHwc->lyr_client_id = secLayerMngRegisterClient(pScrn, "HWC", 1);
    if (pHwc->lyr_client_id == LYR_ERROR_ID)
    {
        XDBG_ERROR(MHWC, "Can't register layer_id\n");
        goto fail_init;
    }
    secLayerMngAddEvent (pHwc->lyr_client_id, EVENT_LAYER_ANNEX, _secLayerAnnexHandler, pHwc);
    secLayerMngAddEvent (pHwc->lyr_client_id, EVENT_LAYER_FREE_COUNTER, _secLayerFreeCounterHandler, pHwc);

    pHwc->gfx_max_layers = secLayerMngGetListOfAccessablePos(pHwc->lyr_client_id, outputlayer, NULL);
    pHwcInfo->maxLayer = pHwc->gfx_max_layers;
    int i;
    for (i = 0; i < SEC_HWC_MAX_LAYER; i++)
    {
        pHwc->lpos[i] = LAYER_NONE;
    }

#endif

    if (LoaderSymbol ("hwc_screen_init"))
    {
        if ( !hwc_screen_init (pScreen, pHwcInfo))
        {
            xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                    "[HWC] hwc_screen_init failed.\n");
            goto fail_init;
        }
    } else
    {
        xf86DrvMsg (pScrn->scrnIndex, X_ERROR,
                "[HWC] hwc_screen_init not exist. XServer doesn't support HWC extension\n");
        goto fail_init;
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[HWC] Enable HWC.\n");

    if (!_secOverlayRegisterEventResourceTypes())
    {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "[HWC] Failed to register EventResourceTypes. \n");
        return FALSE;
    }
    return TRUE;

fail_init:
    free (pHwc);
    pSec->pHwc = NULL;
    if (pHwcInfo)
        free(pHwcInfo);

    return FALSE;
}

void
secHwcDeinit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);

    if (pSec->pHwc)
    {
#ifdef LAYER_MANAGER
        secLayerMngUnRegisterClient(pSec->pHwc->lyr_client_id);
#endif
        free(pSec->pHwc);
        pSec->pHwc = NULL;
    }

    XDBG_INFO(MHWC, "Close HWC.\n");
}
