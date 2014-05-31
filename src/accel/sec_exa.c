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

#include <fcntl.h>
#include <sys/mman.h>
#include "sec.h"
#include "sec_accel.h"
#include "sec_display.h"
#include "sec_crtc.h"
#include <X11/Xatom.h>
#include "windowstr.h"
#include "fbpict.h"
#include "sec_util.h"
#include "sec_converter.h"

static void
_setScreenRotationProperty (ScrnInfoPtr pScrn)
{
    SECPtr pSec = SECPTR (pScrn);
    ScreenPtr pScreen = pScrn->pScreen;
    Atom atom_screen_rotaion;
    WindowPtr pWin = pScreen->root;
    int rc;
    atom_screen_rotaion = MakeAtom ("X_SCREEN_ROTATION", 17, TRUE);
    unsigned int rotation = (unsigned int) pSec->rotate;

    rc = dixChangeWindowProperty (serverClient,
                                  pWin, atom_screen_rotaion, XA_CARDINAL, 32,
                                  PropModeReplace, 1, &rotation, FALSE);
	if (rc != Success)
        XDBG_ERROR (MEXAS, "failed : set X_SCREEN_ROTATION to %d\n", rotation);
}

static void
_secExaBlockHandler (pointer blockData, OSTimePtr pTimeout,
                     pointer pReadmask)
{
    ScreenPtr pScreen = screenInfo.screens[0];
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    /* add screen rotation property to the root window */
    _setScreenRotationProperty (pScrn);

    RemoveBlockAndWakeupHandlers (_secExaBlockHandler /*blockHandler*/,
                                  (void*)NULL /*wakeupHandler*/,
                                  (void*)NULL /*blockData*/);
}

static void
SECExaWaitMarker (ScreenPtr pScreen, int marker)
{
}

static Bool
SECExaPrepareAccess (PixmapPtr pPix, int index)
{
    ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);
    SECPixmapPriv *privPixmap = exaGetPixmapDriverPrivate (pPix);
    int opt = TBM_OPTION_READ;
    tbm_bo_handle bo_handle;

    XDBG_RETURN_VAL_IF_FAIL((privPixmap != NULL), FALSE);
    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_FB &&
        privPixmap->bo == NULL)
    {
        privPixmap->bo = secRenderBoRef(pSec->pFb->default_bo);
        XDBG_RETURN_VAL_IF_FAIL((privPixmap->bo != NULL), FALSE);
        XDBG_TRACE(MEXAS, " FRAMEBUFFER\n");
    }
    else
    {
        XDBG_TRACE(MEXAS, "\n");
    }

    if(privPixmap->bo)
    {
        if (index == EXA_PREPARE_DEST || index == EXA_PREPARE_AUX_DEST)
            opt |= TBM_OPTION_WRITE;

        bo_handle = tbm_bo_map(privPixmap->bo, TBM_DEVICE_CPU, opt);
        pPix->devPrivate.ptr = bo_handle.ptr;
    }
    else
    {
        if(privPixmap->pPixData)
        {
            pPix->devPrivate.ptr = privPixmap->pPixData;
        }
    }

    XDBG_DEBUG (MEXA, "pix:%p index:%d hint:%d ptr:%p\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr);
    return TRUE;
}

static void
SECExaFinishAccess (PixmapPtr pPix, int index)
{
    XDBG_TRACE(MEXAS, "\n");
    if (!pPix)
        return;

    SECPixmapPriv *privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPix);

    if(privPixmap == NULL)
        return;

    if (privPixmap->bo)
        tbm_bo_unmap (privPixmap->bo);

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_FB)
    {
        secRenderBoUnref(privPixmap->bo);
        privPixmap->bo = NULL;
    }

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY)
        secLayerUpdate (secLayerFind (LAYER_OUTPUT_LCD, LAYER_UPPER));

    pPix->devPrivate.ptr = NULL;
    XDBG_DEBUG (MEXA, "pix:%p index:%d hint:%d ptr:%p\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr);
}

static void *
SECExaCreatePixmap (ScreenPtr pScreen, int size, int align)
{
    SECPixmapPriv *privPixmap = calloc (1, sizeof (SECPixmapPriv));

    return privPixmap;
}

static void
SECExaDestroyPixmap (ScreenPtr pScreen, void *driverPriv)
{
    XDBG_RETURN_IF_FAIL (driverPriv != NULL);
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);

    SECPixmapPriv *privPixmap = (SECPixmapPriv*)driverPriv;

    XDBG_TRACE (MEXA, "DESTROY_PIXMAP : usage_hint:0x%x\n", privPixmap->usage_hint);

    switch(privPixmap->usage_hint)
    {
    case CREATE_PIXMAP_USAGE_FB:
        pSec->pix_fb = pSec->pix_fb - privPixmap->size;
        secRenderBoUnref (privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK:
        pSec->pix_dri2_flip_back = pSec->pix_dri2_flip_back - privPixmap->size;
        secRenderBoUnref (privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_SUB_FB:
        /* TODO ???? */
        pSec->pix_sub_fb = pSec->pix_sub_fb - privPixmap->size;
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_OVERLAY:
        /* TODO ???? */
        pSec->pix_overlay = pSec->pix_overlay - privPixmap->size;
        secRenderBoUnref (privPixmap->bo);
        privPixmap->bo = NULL;

        if (privPixmap->ovl_layer)
        {
            secLayerUnref (privPixmap->ovl_layer);
            privPixmap->ovl_layer = NULL;
        }

        pSec->ovl_drawable = NULL;

        SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
        xf86CrtcPtr pCrtc = secCrtcGetAtGeometry (pScrn, 0, 0,
                                                  pSecMode->main_lcd_mode.hdisplay,
                                                  pSecMode->main_lcd_mode.vdisplay);
        secCrtcOverlayRef (pCrtc, FALSE);

        break;
    case CREATE_PIXMAP_USAGE_DRI2_BACK:
        pSec->pix_dri2_back = pSec->pix_dri2_back - privPixmap->size;
        tbm_bo_unref (privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_BACKING_PIXMAP:
        pSec->pix_backing_pixmap = pSec->pix_backing_pixmap - privPixmap->size;
        tbm_bo_unref (privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    default:
        pSec->pix_normal = pSec->pix_normal - privPixmap->size;
        tbm_bo_unref (privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    }

    /* free pixmap private */
    free (privPixmap);
}

static Bool
SECExaModifyPixmapHeader (PixmapPtr pPixmap, int width, int height,
                          int depth, int bitsPerPixel, int devKind, pointer pPixData)
{
    XDBG_RETURN_VAL_IF_FAIL(pPixmap, FALSE);

    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);
    SECPixmapPriv * privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPixmap);
    long lSizeInBytes;

    /* set the default headers of the pixmap */
    miModifyPixmapHeader (pPixmap, width, height, depth, bitsPerPixel,
                          devKind, pPixData);

    /* screen pixmap : set a framebuffer pixmap */
    if (pPixData == (void*)ROOT_FB_ADDR)
    {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pSec->pix_fb = pSec->pix_fb + lSizeInBytes;
        pPixmap->usage_hint = CREATE_PIXMAP_USAGE_FB;
        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = TRUE;
        privPixmap->bo = NULL;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_FB(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;
    }

    if(pPixmap->usage_hint == CREATE_PIXMAP_USAGE_SUB_FB)
    {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pSec->pix_sub_fb = pSec->pix_sub_fb + lSizeInBytes;

        pPixmap->devPrivate.ptr = NULL;
        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isSubFramebuffer = TRUE;
        privPixmap->bo = (tbm_bo)pPixData;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_SUB_FB(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;
    }
    else if(pPixmap->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY)
    {
        SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
        SECLayer *layer;
        SECVideoBuf *vbuf;
        int width, height;

        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pSec->pix_overlay = pSec->pix_overlay + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->size = lSizeInBytes;

        pSec->ovl_drawable = &pPixmap->drawable;

        /* change buffer if needed. */
        xf86CrtcPtr pCrtc = secCrtcGetAtGeometry (pScrn, 0, 0,
                                                  pSecMode->main_lcd_mode.hdisplay,
                                                  pSecMode->main_lcd_mode.vdisplay);
        secCrtcOverlayRef (pCrtc, TRUE);

        layer = secLayerFind (LAYER_OUTPUT_LCD, LAYER_UPPER);
        XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);

        vbuf = secLayerGetBuffer (layer);
        XDBG_RETURN_VAL_IF_FAIL (vbuf != NULL, FALSE);

        width = vbuf->width;
        height = vbuf->height;

        if (width != pSecMode->main_lcd_mode.hdisplay || height != pSecMode->main_lcd_mode.vdisplay)
        {
            XDBG_ERROR (MEXA, "layer size(%d,%d) should be (%dx%d). pixmap(%d,%d %dx%d)\n",
                        width, height, pSecMode->main_lcd_mode.hdisplay, pSecMode->main_lcd_mode.vdisplay,
                        pPixmap->screen_x, pPixmap->screen_y, pPixmap->drawable.width, pPixmap->drawable.height);
            return FALSE;
        }

        privPixmap->bo = secRenderBoRef (vbuf->bo[0]);

        privPixmap->ovl_layer = secLayerRef (layer);

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_OVERLAY(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;
    }
    else if(pPixmap->usage_hint == CREATE_PIXMAP_USAGE_XVIDEO)
    {
        SECCvtProp prop = {0,};
        tbm_bo old_bo = privPixmap->bo;

        prop.id = FOURCC_RGB32;
        prop.width = width;
        prop.height = height;
        prop.crop.width = width;
        prop.crop.height = height;

        secCvtEnsureSize (NULL, &prop);

        privPixmap->bo = secRenderBoCreate(pScrn, prop.width, prop.height);
        if (!privPixmap->bo)
        {
            XDBG_ERROR (MEXA, "Error: cannot create a xvideo buffer\n");
            privPixmap->bo = old_bo;
            return FALSE;
        }

        pPixmap->devKind = prop.width * 4;

        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pSec->pix_dri2_flip_back = pSec->pix_dri2_flip_back + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = FALSE;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_USAGE_XVIDEO(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, privPixmap->bo, pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        if (old_bo)
            tbm_bo_unref (old_bo);

        return TRUE;
    }
    else if(pPixmap->usage_hint == CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK)
    {
        privPixmap->bo = secRenderBoCreate(pScrn, width, height);
        if (!privPixmap->bo)
        {
            XDBG_ERROR (MEXA, "Error: cannot create a back flip buffer\n");
            return FALSE;
        }
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pSec->pix_dri2_flip_back = pSec->pix_dri2_flip_back + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = TRUE;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_DRI2_FLIP_BACK(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, privPixmap->bo, pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;
    }
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_DRI2_BACK)
    {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        privPixmap->usage_hint = pPixmap->usage_hint;

        privPixmap->bo = tbm_bo_alloc (pSec->tbm_bufmgr, lSizeInBytes, TBM_BO_DEFAULT);
        if (privPixmap->bo == NULL)
        {
            XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%d\n",lSizeInBytes);
            return FALSE;
        }
        pSec->pix_dri2_back = pSec->pix_dri2_back + lSizeInBytes;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_USAGE_DRI2_BACK(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, privPixmap->bo,
                    pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;

    }
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP)
    {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        privPixmap->usage_hint = pPixmap->usage_hint;

        privPixmap->bo = tbm_bo_alloc (pSec->tbm_bufmgr, lSizeInBytes, TBM_BO_DEFAULT);
        if (privPixmap->bo == NULL)
        {
            XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%d\n",lSizeInBytes);
            return FALSE;
        }
        pSec->pix_backing_pixmap = pSec->pix_backing_pixmap + lSizeInBytes;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE (MEXA, "CREATE_PIXMAP_USAGE_BACKING_PIXMAP(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                    pPixmap, privPixmap->bo,
                    pPixmap->drawable.x, pPixmap->drawable.y, width, height);

        return TRUE;

    }

    if(privPixmap->bo != NULL)
    {
        tbm_bo_unref (privPixmap->bo);
        privPixmap->bo = NULL;
    }

    lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
    privPixmap->usage_hint = pPixmap->usage_hint;

    /* pPixData is also set for text glyphs or SHM-PutImage */
    if (pPixData)
    {
        privPixmap->pPixData = pPixData;
        /*
        privPixmap->bo = tbm_bo_attach(pSec->tbm_bufmgr,
                                           NULL,
                                           TBM_MEM_USERPTR,
                                           lSizeInBytes, (unsigned int)pPixData);
        */
    }
    else
    {
        /* create the pixmap private memory */
        if (lSizeInBytes && privPixmap->bo == NULL)
        {
            privPixmap->bo = tbm_bo_alloc (pSec->tbm_bufmgr, lSizeInBytes, TBM_BO_DEFAULT);
            if (privPixmap->bo == NULL)
            {
                XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%d\n",lSizeInBytes);
                return FALSE;
            }
        }
        pSec->pix_normal = pSec->pix_normal + lSizeInBytes;
    }

    XDBG_TRACE (MEXA, "CREATE_PIXMAP_NORMAL(%p) : bo:%p, pPixData:%p (%dx%d+%d+%d)\n",
                pPixmap, privPixmap->bo, pPixData,
                width, height,
                pPixmap->drawable.x, pPixmap->drawable.y);

    return TRUE;
}

static Bool
SECExaPixmapIsOffscreen (PixmapPtr pPix)
{
    return TRUE;
}

Bool
secExaInit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    ExaDriverPtr pExaDriver;
    SECPtr pSec = SECPTR (pScrn);
    SECExaPrivPtr pExaPriv;
    unsigned int cpp = 4;

    /* allocate the pExaPriv private */
    pExaPriv = calloc (1, sizeof (*pExaPriv));
    if (pExaPriv == NULL)
        return FALSE;

    /* allocate the EXA driver private */
    pExaDriver = exaDriverAlloc();
    if (pExaDriver == NULL)
    {
        free (pExaPriv);
        return FALSE;
    }

    /* version of exa */
    pExaDriver->exa_major = EXA_VERSION_MAJOR;
    pExaDriver->exa_minor = EXA_VERSION_MINOR;

    /* setting the memory stuffs */
    pExaDriver->memoryBase = (void*)ROOT_FB_ADDR;
    pExaDriver->memorySize = pScrn->videoRam * 1024;
    pExaDriver->offScreenBase = pScrn->displayWidth * cpp * pScrn->virtualY;

    pExaDriver->maxX = 1 << 16;
    pExaDriver->maxY = 1 << 16;
    pExaDriver->pixmapOffsetAlign = 0;
    pExaDriver->pixmapPitchAlign = 8;
    pExaDriver->flags = (EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS
                         |EXA_SUPPORTS_OFFSCREEN_OVERLAPS
                         |EXA_SUPPORTS_PREPARE_AUX);

    pExaDriver->WaitMarker = SECExaWaitMarker;
    pExaDriver->PrepareAccess = SECExaPrepareAccess;
    pExaDriver->FinishAccess = SECExaFinishAccess;

    pExaDriver->CreatePixmap = SECExaCreatePixmap;
    pExaDriver->DestroyPixmap = SECExaDestroyPixmap;
    pExaDriver->ModifyPixmapHeader = SECExaModifyPixmapHeader;
    pExaDriver->PixmapIsOffscreen = SECExaPixmapIsOffscreen;

    /* call init function */
    if (pSec->is_sw_exa)
    {
        if (secExaSwInit (pScreen, pExaDriver))
        {
            XDBG_INFO (MEXA, "Initialized SEC SW_EXA acceleration OK !\n");
        }
        else
        {
            free (pExaPriv);
            free (pExaDriver);
            FatalError ("Failed to initialize SW_EXA\n");
            return FALSE;
        }
    }
    else
    {
        if (secExaG2dInit (pScreen, pExaDriver))
        {
            XDBG_INFO (MEXA, "Initialized SEC HW_EXA acceleration OK !\n");
        }
        else
        {
            free (pExaPriv);
            free (pExaDriver);
            FatalError ("Failed to initialize HW_EXA\n");
            return FALSE;
        }
    }

    /* exa driver init with exa driver private */
    if (exaDriverInit (pScreen, pExaDriver))
    {
        pExaPriv->pExaDriver = pExaDriver;
        pSec->pExaPriv = pExaPriv;
    }
    else
    {
        free (pExaDriver);
        free (pExaPriv);
        FatalError ("Failed to initialize EXA...exaDriverInit\n");
        return FALSE;
    }

    /* block handler */
    RegisterBlockAndWakeupHandlers (_secExaBlockHandler /*blockHandler*/,
                                    NULL /*wakeupHandler*/,
                                    NULL /*blockData*/);

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "EXA driver is Loaded successfully\n");

    return TRUE;
}

void
secExaDeinit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);

    /* call Fini function */
    if (pSec->is_sw_exa)
    {
        secExaSwDeinit (pScreen);
        XDBG_INFO (MEXA, "Finish SW EXA acceleration.\n");
    }

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "EXA driver is UnLoaded successfully\n");
}

Bool
secExaPrepareAccess (PixmapPtr pPix, int index)
{
    return SECExaPrepareAccess (pPix, index);
}

void
secExaFinishAccess (PixmapPtr pPix, int index)
{
    SECExaFinishAccess (pPix, index);
}

int
secExaScreenAsyncSwap (ScreenPtr pScreen, int enable)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);

    if(enable == -1)
        return pSec->useAsyncSwap;

    if ( enable == 1)
        pSec->useAsyncSwap = TRUE;
    else
        pSec->useAsyncSwap = FALSE;

    return pSec->useAsyncSwap;
}

int
secExaScreenSetScrnPixmap (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PixmapPtr pPix = (*pScreen->GetScreenPixmap) (pScreen);
    unsigned int pitch = pScrn->virtualX * 4;
    (*pScreen->ModifyPixmapHeader) (pPix, pScrn->virtualX, pScrn->virtualY,
                                    -1, -1, pitch, (void*)ROOT_FB_ADDR);
    pScrn->displayWidth = pitch / 4;
    return 1;
}

Bool
secExaMigratePixmap (PixmapPtr pPix, tbm_bo bo)
{
    SECPixmapPriv *privPixmap = exaGetPixmapDriverPrivate (pPix);

    tbm_bo_unref (privPixmap->bo);
    privPixmap->bo = tbm_bo_ref(bo);

    return TRUE;
}

tbm_bo
secExaPixmapGetBo (PixmapPtr pPix)
{
    tbm_bo bo = NULL;
    SECPixmapPriv *pExaPixPriv = NULL;

    if (pPix == NULL)
        return 0;

    pExaPixPriv = exaGetPixmapDriverPrivate (pPix);
    if (pExaPixPriv == NULL)
        return 0;

    bo = pExaPixPriv->bo;

    return bo;
}
