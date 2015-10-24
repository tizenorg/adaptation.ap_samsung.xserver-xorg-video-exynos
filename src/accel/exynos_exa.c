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
#include "exynos.h"
#include "exynos_accel.h"
#include "exynos_display.h"
#include "exynos_crtc.h"
#include <X11/Xatom.h>
#include "windowstr.h"
#include "fbpict.h"
#include "exynos_util.h"
#include "exynos_converter.h"

static void
_setScreenRotationProperty(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    ScreenPtr pScreen = pScrn->pScreen;
    Atom atom_screen_rotaion;
    WindowPtr pWin = pScreen->root;
    int rc;

    atom_screen_rotaion = MakeAtom("X_SCREEN_ROTATION", 17, TRUE);
    unsigned int rotation = (unsigned int) pExynos->rotate;

    rc = dixChangeWindowProperty(serverClient,
                                 pWin, atom_screen_rotaion, XA_CARDINAL, 32,
                                 PropModeReplace, 1, &rotation, FALSE);
    if (rc != Success)
        XDBG_ERROR(MEXAS, "failed : set X_SCREEN_ROTATION to %d\n", rotation);
}

static void
_exynosExaBlockHandler(pointer blockData, OSTimePtr pTimeout, pointer pReadmask)
{
    ScreenPtr pScreen = screenInfo.screens[0];
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    /* add screen rotation property to the root window */
    _setScreenRotationProperty(pScrn);

    RemoveBlockAndWakeupHandlers(_exynosExaBlockHandler /*blockHandler */ ,
                                 (void *) NULL /*wakeupHandler */ ,
                                 (void *) NULL /*blockData */ );
}

static void
EXYNOSExaWaitMarker(ScreenPtr pScreen, int marker)
{
}

static Bool
EXYNOSExaPrepareAccess(PixmapPtr pPix, int index)
{
    ScrnInfoPtr pScrn = xf86Screens[pPix->drawable.pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSPixmapPriv *privPixmap = exaGetPixmapDriverPrivate(pPix);
    int opt = TBM_OPTION_READ;
    tbm_bo_handle bo_handle;

    XDBG_RETURN_VAL_IF_FAIL((privPixmap != NULL), FALSE);
    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_FB && privPixmap->bo == NULL) {
        privPixmap->bo = exynosRenderBoRef(pExynos->pFb->default_bo);
        XDBG_RETURN_VAL_IF_FAIL((privPixmap->bo != NULL), FALSE);
        privPixmap->set_bo_to_null = TRUE;
        XDBG_TRACE(MEXAS, " FRAMEBUFFER\n");
    }
    else {
        XDBG_TRACE(MEXAS, "\n");
    }

    if (privPixmap->bo) {
        if (index == EXA_PREPARE_DEST || index == EXA_PREPARE_AUX_DEST)
            opt |= TBM_OPTION_WRITE;

        bo_handle = tbm_bo_map(privPixmap->bo, TBM_DEVICE_CPU, opt);
        pPix->devPrivate.ptr = bo_handle.ptr;
    }
    else {
        if (privPixmap->pPixData) {
            pPix->devPrivate.ptr = privPixmap->pPixData;
        }
    }

    XDBG_DEBUG(MEXA, "pix:%p index:%d hint:%d ptr:%p\n",
               pPix, index, pPix->usage_hint, pPix->devPrivate.ptr);
    return TRUE;
}

static void
EXYNOSExaFinishAccess(PixmapPtr pPix, int index)
{
    XDBG_TRACE(MEXAS, "\n");
    if (!pPix)
        return;

    EXYNOSPixmapPriv *privPixmap =
        (EXYNOSPixmapPriv *) exaGetPixmapDriverPrivate(pPix);

    if (privPixmap == NULL)
        return;

    if (privPixmap->bo)
        tbm_bo_unmap(privPixmap->bo);

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_FB &&
        privPixmap->set_bo_to_null) {
        exynosRenderBoUnref(privPixmap->bo);
        privPixmap->bo = NULL;
        privPixmap->set_bo_to_null = FALSE;
    }
/*
    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY)
        exynosLayerUpdate (exynosLayerFind (LAYER_OUTPUT_LCD, LAYER_UPPER));
*/
    XDBG_DEBUG(MEXA, "pix:%p index:%d hint:%d ptr:%p\n",
               pPix, index, pPix->usage_hint, pPix->devPrivate.ptr);
    pPix->devPrivate.ptr = NULL;

}

static void *
EXYNOSExaCreatePixmap(ScreenPtr pScreen, int size, int align)
{
    TTRACE_GRAPHICS_BEGIN("XORG:EXA:CREATE_PIXMAP");

    EXYNOSPixmapPriv *privPixmap = calloc(1, sizeof(EXYNOSPixmapPriv));

    TTRACE_GRAPHICS_END();

    return privPixmap;
}

static void
EXYNOSExaDestroyPixmap(ScreenPtr pScreen, void *driverPriv)
{
    TTRACE_GRAPHICS_BEGIN("XORG:EXA:DESTROY_PIXMAP");

    XDBG_RETURN_IF_FAIL(driverPriv != NULL);
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    EXYNOSPixmapPriv *privPixmap = (EXYNOSPixmapPriv *) driverPriv;

    XDBG_TRACE(MEXA, "DESTROY_PIXMAP : bo:%p name:%d usage_hint:0x%x\n",
               privPixmap->bo, tbm_bo_export(privPixmap->bo),
               privPixmap->usage_hint);

    switch (privPixmap->usage_hint) {
    case CREATE_PIXMAP_USAGE_FB:
        pExynos->pix_fb = pExynos->pix_fb - privPixmap->size;
        exynosRenderBoUnref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK:
        pExynos->pix_dri2_flip_back =
            pExynos->pix_dri2_flip_back - privPixmap->size;
        exynosRenderBoUnref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_SUB_FB:
        /* TODO ???? */
        pExynos->pix_sub_fb = pExynos->pix_sub_fb - privPixmap->size;
        privPixmap->bo = NULL;
        break;
#if 0
    case CREATE_PIXMAP_USAGE_OVERLAY:
        /* TODO ???? */
        pExynos->pix_overlay = pExynos->pix_overlay - privPixmap->size;
        exynosRenderBoUnref(privPixmap->bo);
        privPixmap->bo = NULL;

        if (privPixmap->ovl_layer) {
            exynosLayerUnref(privPixmap->ovl_layer);
            privPixmap->ovl_layer = NULL;
        }

        pExynos->ovl_drawable = NULL;

        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
        xf86CrtcPtr pCrtc = exynosCrtcGetAtGeometry(pScrn, 0, 0,
                                                    pExynosMode->main_lcd_mode.
                                                    hdisplay,
                                                    pExynosMode->main_lcd_mode.
                                                    vdisplay);
        exynosCrtcOverlayRef(pCrtc, FALSE);

        break;
#endif
    case CREATE_PIXMAP_USAGE_DRI2_BACK:
        pExynos->pix_dri2_back = pExynos->pix_dri2_back - privPixmap->size;
        tbm_bo_unref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_DRI3_BACK:
        pExynos->pix_dri3_back = pExynos->pix_dri3_back - privPixmap->size;
        tbm_bo_unref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    case CREATE_PIXMAP_USAGE_BACKING_PIXMAP:
    case CREATE_PIXMAP_USAGE_OVERLAY:
        pExynos->pix_backing_pixmap =
            pExynos->pix_backing_pixmap - privPixmap->size;
        tbm_bo_unref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    default:
        pExynos->pix_normal = pExynos->pix_normal - privPixmap->size;
        tbm_bo_unref(privPixmap->bo);
        privPixmap->bo = NULL;
        break;
    }

    /* free pixmap private */
    free(privPixmap);
    TTRACE_GRAPHICS_END();
}

static Bool
EXYNOSExaModifyPixmapHeader(PixmapPtr pPixmap, int width, int height,
                            int depth, int bitsPerPixel, int devKind,
                            pointer pPixData)
{
    XDBG_RETURN_VAL_IF_FAIL(pPixmap, FALSE);

    TTRACE_GRAPHICS_BEGIN("XORG:EXA:MODIFY_PIXMAP_HEADER");

    ScrnInfoPtr pScrn = xf86Screens[pPixmap->drawable.pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSPixmapPriv *privPixmap =
        (EXYNOSPixmapPriv *) exaGetPixmapDriverPrivate(pPixmap);
    long lSizeInBytes;

    /* set the default headers of the pixmap */
    miModifyPixmapHeader(pPixmap, width, height, depth, bitsPerPixel,
                         devKind, pPixData);

    /* screen pixmap : set a framebuffer pixmap */
    if (pPixData == (void *) ROOT_FB_ADDR) {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pExynos->pix_fb = pExynos->pix_fb + lSizeInBytes;
        pPixmap->usage_hint = CREATE_PIXMAP_USAGE_FB;
        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = TRUE;
        privPixmap->bo = NULL;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA, "CREATE_PIXMAP_FB(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width,
                   height);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }

    if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_SUB_FB) {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pExynos->pix_sub_fb = pExynos->pix_sub_fb + lSizeInBytes;

        pPixmap->devPrivate.ptr = NULL;
        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isSubFramebuffer = TRUE;
        privPixmap->bo = (tbm_bo) pPixData;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA, "CREATE_PIXMAP_SUB_FB(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width,
                   height);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }
#if 0
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY) {
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
        EXYNOSLayer *layer;
        EXYNOSVideoBuf *vbuf;
        int width, height;

        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pExynos->pix_overlay = pExynos->pix_overlay + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->size = lSizeInBytes;

        pExynos->ovl_drawable = &pPixmap->drawable;

        /* change buffer if needed. */
        xf86CrtcPtr pCrtc = exynosCrtcGetAtGeometry(pScrn, 0, 0,
                                                    pExynosMode->main_lcd_mode.
                                                    hdisplay,
                                                    pExynosMode->main_lcd_mode.
                                                    vdisplay);
        exynosCrtcOverlayRef(pCrtc, TRUE);

        layer = exynosLayerFind(LAYER_OUTPUT_LCD, LAYER_UPPER);
        XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);

        vbuf = exynosLayerGetBuffer(layer);
        XDBG_RETURN_VAL_IF_FAIL(vbuf != NULL, FALSE);

        width = vbuf->width;
        height = vbuf->height;

        if (width != pExynosMode->main_lcd_mode.hdisplay ||
            height != pExynosMode->main_lcd_mode.vdisplay) {
            XDBG_ERROR(MEXA,
                       "layer size(%d,%d) should be (%dx%d). pixmap(%d,%d %dx%d)\n",
                       width, height, pExynosMode->main_lcd_mode.hdisplay,
                       pExynosMode->main_lcd_mode.vdisplay, pPixmap->screen_x,
                       pPixmap->screen_y, pPixmap->drawable.width,
                       pPixmap->drawable.height);
            return FALSE;
        }

        privPixmap->bo = exynosRenderBoRef(vbuf->bo[0]);

        privPixmap->ovl_layer = exynosLayerRef(layer);

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_OVERLAY(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width,
                   height);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }
#endif
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_XVIDEO) {
        EXYNOSCvtProp prop = { 0, };
        tbm_bo old_bo = privPixmap->bo;

        prop.id = FOURCC_RGB32;
        prop.width = width;
        prop.height = height;
        prop.crop.width = width;
        prop.crop.height = height;

        if (!exynosCvtEnsureSize(NULL, &prop))
            return FALSE;

        privPixmap->bo = exynosRenderBoCreate(pScrn, prop.width, prop.height);
        if (!privPixmap->bo) {
            XDBG_ERROR(MEXA, "Error: cannot create a xvideo buffer\n");
            privPixmap->bo = old_bo;

            TTRACE_GRAPHICS_END();
            return FALSE;
        }

        pPixmap->devKind = prop.width * 4;

        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pExynos->pix_dri2_flip_back =
            pExynos->pix_dri2_flip_back + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = FALSE;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_USAGE_XVIDEO(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, privPixmap->bo, pPixmap->drawable.x,
                   pPixmap->drawable.y, width, height);

        if (old_bo)
            tbm_bo_unref(old_bo);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_DRI2_FLIP_BACK) {
        privPixmap->bo =
            exynosRenderBoCreate(pScrn, pPixmap->devKind / 4, height);
        if (!privPixmap->bo) {
            XDBG_ERROR(MEXA, "Error: cannot create a back flip buffer\n");
            TTRACE_GRAPHICS_END();
            return FALSE;
        }
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        pExynos->pix_dri2_flip_back =
            pExynos->pix_dri2_flip_back + lSizeInBytes;

        privPixmap->usage_hint = pPixmap->usage_hint;
        privPixmap->isFrameBuffer = TRUE;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_DRI2_FLIP_BACK(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, privPixmap->bo, pPixmap->drawable.x,
                   pPixmap->drawable.y, width, height);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_DRI2_BACK) {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        privPixmap->usage_hint = pPixmap->usage_hint;

        if (pExynos->use_hwc && pExynos->hwc_active)
            privPixmap->bo =
                exynosRenderBoCreate(pScrn, pPixmap->devKind / 4, height);
        else
            privPixmap->bo =
                tbm_bo_alloc(pExynos->tbm_bufmgr, lSizeInBytes, TBM_BO_DEFAULT);
        if (privPixmap->bo == NULL) {
            XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%ld\n",
                       lSizeInBytes);
            TTRACE_GRAPHICS_END();
            return FALSE;
        }
        pExynos->pix_dri2_back = pExynos->pix_dri2_back + lSizeInBytes;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_USAGE_DRI2_BACK(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, privPixmap->bo, pPixmap->drawable.x,
                   pPixmap->drawable.y, width, height);

        TTRACE_GRAPHICS_END();
        return TRUE;

    }
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_DRI3_BACK) {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        privPixmap->usage_hint = pPixmap->usage_hint;

        /* [cyeon] pixmap tbm bo will be attached in EXYNOSDRI3PixmapFromFd
         */
        privPixmap->bo = NULL;

        pExynos->pix_dri3_back = pExynos->pix_dri3_back + lSizeInBytes;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_USAGE_DRI3_BACK(%p) : (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, pPixmap->drawable.x, pPixmap->drawable.y, width,
                   height);

        TTRACE_GRAPHICS_END();
        return TRUE;
    }
#if 1
    else if (pPixmap->usage_hint == CREATE_PIXMAP_USAGE_BACKING_PIXMAP ||
             pPixmap->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY) {
        lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
        privPixmap->usage_hint = pPixmap->usage_hint;

        /*
         * Each "backing pixmap" can be used as frame buffer through HWC extension.
         * We may use a map/unmap construction in HWC extension before
         * frame buffer will be used (look drmModeSetPlane()), but this way
         * works better (without artifacts).
         */
        if (pExynos->use_hwc && pExynos->hwc_active)
            privPixmap->bo =
                exynosRenderBoCreate(pScrn, pPixmap->devKind / 4, height);
        else
            privPixmap->bo =
                tbm_bo_alloc(pExynos->tbm_bufmgr, lSizeInBytes, TBM_BO_DEFAULT);
        if (privPixmap->bo == NULL) {
            XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%ld\n",
                       lSizeInBytes);
            TTRACE_GRAPHICS_END();
            return FALSE;
        }
        pExynos->pix_backing_pixmap =
            pExynos->pix_backing_pixmap + lSizeInBytes;
        privPixmap->size = lSizeInBytes;

        XDBG_TRACE(MEXA,
                   "CREATE_PIXMAP_USAGE_BACKING_PIXMAP(%p) : bo:%p (x,y,w,h)=(%d,%d,%d,%d)\n",
                   pPixmap, privPixmap->bo, pPixmap->drawable.x,
                   pPixmap->drawable.y, width, height);

        TTRACE_GRAPHICS_END();
        return TRUE;

    }
#endif
    if (privPixmap->bo != NULL) {
        tbm_bo_unref(privPixmap->bo);
        privPixmap->bo = NULL;
    }

    lSizeInBytes = pPixmap->drawable.height * pPixmap->devKind;
    privPixmap->usage_hint = pPixmap->usage_hint;

    /* pPixData is also set for text glyphs or SHM-PutImage */
    if (pPixData) {
        privPixmap->pPixData = pPixData;
        /*
           privPixmap->bo = tbm_bo_attach(pExynos->tbm_bufmgr,
           NULL,
           TBM_MEM_USERPTR,
           lSizeInBytes, (unsigned int)pPixData);
         */
    }
    else {
        /* create the pixmap private memory */
        if (lSizeInBytes && privPixmap->bo == NULL) {
            if (pExynos->hwc_active)
                privPixmap->bo =
                    exynosRenderBoCreate(pScrn, pPixmap->devKind / 4, height);
            else
                privPixmap->bo =
                    tbm_bo_alloc(pExynos->tbm_bufmgr, lSizeInBytes,
                                 TBM_BO_DEFAULT);
            if (privPixmap->bo == NULL) {
                XDBG_ERROR(MEXA, "Error on allocating BufferObject. size:%ld\n",
                           lSizeInBytes);
                TTRACE_GRAPHICS_END();
                return FALSE;
            }
        }
        pExynos->pix_normal = pExynos->pix_normal + lSizeInBytes;
    }

    XDBG_TRACE(MEXA,
               "CREATE_PIXMAP_NORMAL(%p) : bo:%p, pPixData:%p (%dx%d+%d+%d)\n",
               pPixmap, privPixmap->bo, pPixData, width, height,
               pPixmap->drawable.x, pPixmap->drawable.y);

    TTRACE_GRAPHICS_END();
    return TRUE;
}

static Bool
EXYNOSExaPixmapIsOffscreen(PixmapPtr pPix)
{
    return TRUE;
}

Bool
exynosExaInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    ExaDriverPtr pExaDriver;
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSExaPrivPtr pExaPriv;
    unsigned int cpp = 4;

    /* allocate the pExaPriv private */
    pExaPriv = calloc(1, sizeof(*pExaPriv));
    if (pExaPriv == NULL)
        return FALSE;

    /* allocate the EXA driver private */
    pExaDriver = exaDriverAlloc();
    if (pExaDriver == NULL) {
        free(pExaPriv);
        return FALSE;
    }

    /* version of exa */
    pExaDriver->exa_major = EXA_VERSION_MAJOR;
    pExaDriver->exa_minor = EXA_VERSION_MINOR;

    /* setting the memory stuffs */
    pExaDriver->memoryBase = (void *) ROOT_FB_ADDR;
    pExaDriver->memorySize = pScrn->videoRam * 1024;
    pExaDriver->offScreenBase = pScrn->displayWidth * cpp * pScrn->virtualY;

    pExaDriver->maxX = 1 << 16;
    pExaDriver->maxY = 1 << 16;
    pExaDriver->pixmapOffsetAlign = 0;
    pExaDriver->pixmapPitchAlign = 64;
    pExaDriver->flags = (EXA_OFFSCREEN_PIXMAPS | EXA_HANDLES_PIXMAPS
                         | EXA_SUPPORTS_OFFSCREEN_OVERLAPS
                         | EXA_SUPPORTS_PREPARE_AUX);

    pExaDriver->WaitMarker = EXYNOSExaWaitMarker;
    pExaDriver->PrepareAccess = EXYNOSExaPrepareAccess;
    pExaDriver->FinishAccess = EXYNOSExaFinishAccess;

    pExaDriver->CreatePixmap = EXYNOSExaCreatePixmap;
    pExaDriver->DestroyPixmap = EXYNOSExaDestroyPixmap;
    pExaDriver->ModifyPixmapHeader = EXYNOSExaModifyPixmapHeader;
    pExaDriver->PixmapIsOffscreen = EXYNOSExaPixmapIsOffscreen;

    /* call init function */
    if (pExynos->is_sw_exa) {
        if (exynosExaSwInit(pScreen, pExaDriver)) {
            XDBG_INFO(MEXA, "Initialized EXYNOS SW_EXA acceleration OK !\n");
        }
        else {
            free(pExaPriv);
            free(pExaDriver);
            FatalError("Failed to initialize SW_EXA\n");
            return FALSE;
        }
    }

    /* exa driver init with exa driver private */
    if (exaDriverInit(pScreen, pExaDriver)) {
        pExaPriv->pExaDriver = pExaDriver;
        pExynos->pExaPriv = pExaPriv;
    }
    else {
        free(pExaDriver);
        free(pExaPriv);
        FatalError("Failed to initialize EXA...exaDriverInit\n");
        return FALSE;
    }

    /* block handler */
    RegisterBlockAndWakeupHandlers(_exynosExaBlockHandler /*blockHandler */ ,
                                   NULL /*wakeupHandler */ ,
                                   NULL /*blockData */ );

    xf86DrvMsg(pScrn->scrnIndex, X_INFO, "EXA driver is Loaded successfully\n");

    return TRUE;
}

void
exynosExaDeinit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    /* call Fini function */
    if (pExynos->is_sw_exa) {
        exynosExaSwDeinit(pScreen);
        XDBG_INFO(MEXA, "Finish SW EXA acceleration.\n");
    }

    xf86DrvMsg(pScrn->scrnIndex, X_INFO,
               "EXA driver is UnLoaded successfully\n");
}

Bool
exynosExaPrepareAccess(PixmapPtr pPix, int index)
{
    return EXYNOSExaPrepareAccess(pPix, index);
}

void
exynosExaFinishAccess(PixmapPtr pPix, int index)
{
    EXYNOSExaFinishAccess(pPix, index);
}

int
exynosExaScreenAsyncSwap(ScreenPtr pScreen, int enable)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (enable == -1)
        return pExynos->useAsyncSwap;

    if (enable == 1)
        pExynos->useAsyncSwap = TRUE;
    else
        pExynos->useAsyncSwap = FALSE;

    return pExynos->useAsyncSwap;
}

int
exynosExaScreenSetScrnPixmap(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    PixmapPtr pPix = (*pScreen->GetScreenPixmap) (pScreen);
    unsigned int pitch = pScrn->virtualX * 4;

    (*pScreen->ModifyPixmapHeader) (pPix, pScrn->virtualX, pScrn->virtualY,
                                    -1, -1, pitch, (void *) ROOT_FB_ADDR);
    pScrn->displayWidth = pitch / 4;
    return 1;
}

Bool
exynosExaMigratePixmap(PixmapPtr pPix, tbm_bo bo)
{
    EXYNOSPixmapPriv *privPixmap = exaGetPixmapDriverPrivate(pPix);

    if (privPixmap->bo)
        tbm_bo_unref(privPixmap->bo);
    privPixmap->bo = tbm_bo_ref(bo);

    return TRUE;
}

tbm_bo
exynosExaPixmapGetBo(PixmapPtr pPix)
{
    tbm_bo bo = NULL;
    EXYNOSPixmapPriv *pExaPixPriv = NULL;

    if (pPix == NULL)
        return 0;

    pExaPixPriv = exaGetPixmapDriverPrivate(pPix);
    if (pExaPixPriv == NULL)
        return 0;

    bo = pExaPixPriv->bo;

    return bo;
}
