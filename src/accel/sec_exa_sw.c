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

#include "sec.h"
#include "sec_accel.h"
#include "sec_util.h"
#include "sec_layer.h"
#include "exa.h"
#include "fbpict.h"
#include "neonmem.h"

typedef struct
{
    BoxRec  pos;
    PixmapPtr pixmap;
    tbm_bo bo;
    void* addr;
} ExaOpBuf;

typedef struct
{
    int refcnt;
    int opt;
    int num;
    int isSame;

    ExaOpBuf buf[5];
} ExaOpInfo;

typedef struct
{
    BoxRec box;
    int state;            /*state of region*/

    struct xorg_list link;

    ExaOpBuf *pSrc;
    ExaOpBuf *pMask;
    ExaOpBuf *pDst;
} ExaBox;

typedef struct
{
    Bool bDo;

    int alu;
    Pixel planemask;
    Pixel fg;
    PixmapPtr pixmap;

    int x,y,w,h;
    GCPtr pGC;
    ExaOpInfo* pOpDst;
    struct xorg_list opBox;
} OpSolid;

typedef struct
{
    Bool bDo;
    Pixel pm;
    int alu;
    int reverse;
    int upsidedown;
    PixmapPtr pSrcPix;
    PixmapPtr pDstPix;

    /*copy param*/
    int srcX;
    int srcY;
    int dstX;
    int dstY;
    int width, height;

    ExaOpInfo* pOpDst;
    ExaOpInfo* pOpSrc;
    struct xorg_list opBox;
} OpCopy;

typedef struct
{
    Bool bDo;
    int op;

    PicturePtr pSrcPicture;
    PicturePtr pMaskPicture;
    PicturePtr pDstPicture;
    PixmapPtr pSrcPixmap;
    PixmapPtr pMaskPixmap;
    PixmapPtr pDstPixmap;

    /*copy param*/
    int srcX, srcY;
    int maskX, maskY;
    int dstX, dstY;
    int width, height;

    ExaOpInfo* pOpSrc;
    ExaOpInfo* pOpMask;
    ExaOpInfo* pOpDst;
    struct xorg_list opBox;
} OpComposite;

typedef struct
{
    Bool bDo;

    PixmapPtr pDst;
    int x,y,w,h;
    char* src;
    int src_pitch;

    ExaOpInfo* pOpDst;
    struct xorg_list opBox;
} OpUTS;

typedef struct
{
    Bool bDo;

    PixmapPtr pSrc;
    int x,y,w,h;
    char* dst;
    int dst_pitch;

    ExaOpInfo* pOpSrc;
    struct xorg_list opBox;
} OpDFS;

typedef void (* DoDrawProcPtr) (PixmapPtr pPix, Bool isPart,
                                int x, int y,
                                int clip_x, int clip_y,
                                int w, int h, void* data);

typedef void (* DoDrawProcPtrEx) (ExaBox* box, void* data);

static ExaOpInfo OpInfo[EXA_NUM_PREPARE_INDICES];
static OpSolid gOpSolid;
static OpCopy gOpCopy;
static OpComposite gOpComposite;
static OpUTS gOpUTS;
static OpDFS gOpDFS;

ExaBox* _swBoxAdd (struct xorg_list *l, BoxPtr b1, BoxPtr b2)
{
    ExaBox* rgn;

    rgn = calloc (1, sizeof (ExaBox));
    rgn->state = secUtilBoxIntersect (&rgn->box, b1, b2);
    if (rgnOUT == rgn->state)
    {
        free (rgn);
        return NULL;
    }

    xorg_list_add (&rgn->link, l);
    return rgn;
}

void _swBoxMerge (struct xorg_list *l, struct xorg_list* b, struct xorg_list* t)
{
    ExaBox *b1 = NULL, *b2 = NULL;
    ExaBox* r=NULL;

    xorg_list_for_each_entry (b1, b, link)
    {
        xorg_list_for_each_entry (b2, t, link)
        {
            r = _swBoxAdd (l, &b1->box, &b2->box);
            if (r)
            {
                r->pSrc = b1->pSrc ? b1->pSrc : b2->pSrc;
                r->pMask= b1->pMask ? b1->pMask : b2->pMask;
                r->pDst = b1->pDst ? b1->pDst : b2->pDst;
            }
        }
    }
}

void _swBoxMove (struct xorg_list* l, int tx, int ty)
{
    ExaBox *b = NULL;

    xorg_list_for_each_entry (b, l, link)
    {
        secUtilBoxMove (&b->box, tx, ty);
    }
}

void _swBoxRemoveAll (struct xorg_list* l)
{
    ExaBox *ref = NULL, *next = NULL;

    xorg_list_for_each_entry_safe (ref, next, l, link)
    {
        xorg_list_del (&ref->link);
        free (ref);
    }
}

int _swBoxIsOne (struct xorg_list* l)
{
    if (l->next != l)
    {
        if (l->next == l->prev)
            return 1;
        else
            return -1;
    }

    return 0;
}

void _swBoxPrint (ExaBox* sb1, const char* name)
{
    ExaBox *b = NULL;

    xorg_list_for_each_entry (b, &sb1->link, link)
    {
        XDBG_DEBUG (MEXA, "[%s] %d,%d - %d,%d\n", name,
                b->box.x1, b->box.y1, b->box.x2, b->box.y2);
    }
}

static ExaOpInfo* _swPrepareAccess (PixmapPtr pPix, int index)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);
    SECPixmapPriv *privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPix);
    ExaOpInfo* op = &OpInfo[index];
    int opt = TBM_OPTION_READ;
    int i;
    tbm_bo *bos;
    tbm_bo_handle bo_handle;
    SECFbBoDataPtr bo_data;
    int num_bo;
    int ret;

    XDBG_RETURN_VAL_IF_FAIL ((privPixmap != NULL), NULL);

    if (index == EXA_PREPARE_DEST || index == EXA_PREPARE_AUX_DEST)
        opt |= TBM_OPTION_WRITE;

    /* Check mapped */
    if (privPixmap->exaOpInfo)
    {
        op = (ExaOpInfo*)privPixmap->exaOpInfo;
        op->refcnt++;
        XDBG_TRACE (MEXAS, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
                    pPix, index, pPix->usage_hint, pPix->devPrivate.ptr, op->refcnt);
        return op;
    }

    /*Set buffer info*/
    memset (op, 0x00, sizeof (ExaOpInfo));
    op->refcnt = 1;
    op->opt = opt;
    op->isSame = 0;

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_FB)
    {
        ret = secFbFindBo (pSec->pFb,
                           pPix->drawable.x, pPix->drawable.y,
                           pPix->drawable.width, pPix->drawable.height,
                           &num_bo, &bos);
        XDBG_TRACE (MEXAS,"FB  ret:%d num_pix:%d, %dx%d+%d+%d\n",
                    ret, num_bo,
                    pPix->drawable.width, pPix->drawable.height,
                    pPix->drawable.x, pPix->drawable.y);

        if (ret == rgnSAME && num_bo == 1)
        {
            op->num = 1;
            op->isSame = 1;

            op->buf[0].pixmap = pPix;
            op->buf[0].bo = bos[0];
            bo_handle = tbm_bo_map (op->buf[0].bo, TBM_DEVICE_CPU, op->opt);
            op->buf[0].addr = bo_handle.ptr;
            op->buf[0].pixmap->devPrivate.ptr = op->buf[0].addr;
            op->buf[0].pos.x1 = 0;
            op->buf[0].pos.y1 = 0;
            op->buf[0].pos.x2 = pPix->drawable.width;
            op->buf[0].pos.y2 = pPix->drawable.height;
        }
        else
        {
            op->num = num_bo;
            op->isSame = 0;

            for (i = 0; i < num_bo; i++)
            {
                tbm_bo_get_user_data (bos[i], TBM_BO_DATA_FB, (void**)&bo_data);
                op->buf[i].pixmap = secRenderBoGetPixmap (pSec->pFb, bos[i]);
                op->buf[i].bo = bos[i];
                bo_handle = tbm_bo_map (bos[i], TBM_DEVICE_CPU, op->opt);
                op->buf[i].addr = bo_handle.ptr;
                op->buf[i].pixmap->devPrivate.ptr = op->buf[i].addr;
                op->buf[i].pos = bo_data->pos;
            }
        }

        if (bos)
        {
            free (bos);
            bos=NULL;
        }
    }
    else
    {
        op->num = 1;
        op->isSame = 1;

        op->buf[0].pixmap = pPix;
        if (privPixmap->bo)
        {
            op->buf[0].bo = privPixmap->bo;
            bo_handle = tbm_bo_map (op->buf[0].bo, TBM_DEVICE_CPU, op->opt);
            op->buf[0].addr = bo_handle.ptr;
        }
        else
        {
            op->buf[0].bo = privPixmap->bo;
            op->buf[0].addr = privPixmap->pPixData;
        }
        op->buf[0].pixmap->devPrivate.ptr = op->buf[0].addr;
        op->buf[0].pos.x1 = 0;
        op->buf[0].pos.y1 = 0;
        op->buf[0].pos.x2 = pPix->drawable.width;
        op->buf[0].pos.y2 = pPix->drawable.height;
    }

    privPixmap->exaOpInfo = op;

    XDBG_TRACE (MEXAS, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr, op->refcnt);
    return op;
}

static void _swFinishAccess (PixmapPtr pPix, int index)
{
    XDBG_RETURN_IF_FAIL (pPix!=NULL);

    SECPixmapPriv *privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPix);
    ExaOpInfo* op;
    int i;

    XDBG_RETURN_IF_FAIL (privPixmap!=NULL);
    XDBG_RETURN_IF_FAIL (privPixmap->exaOpInfo!=NULL);

    op = (ExaOpInfo*)privPixmap->exaOpInfo;
    op->refcnt --;

    if (op->refcnt == 0)
    {
        for (i=0; i < op->num; i++)
        {
            if (op->buf[i].bo)
            {
                tbm_bo_unmap (op->buf[i].bo);

                if( index == EXA_PREPARE_DEST && pPix->usage_hint == CREATE_PIXMAP_USAGE_FB )
                {
                    // In this case, DEST is framebuffer. It is updated by CPU.
                    // After that LCD will use this buffer.
                    // So we should call cache op!!
                    tbm_bo_map(op->buf[i].bo, TBM_DEVICE_3D, TBM_OPTION_READ);
                    tbm_bo_unmap(op->buf[i].bo);

                    ScreenPtr pScreen;
                    pScreen = pPix->drawable.pScreen;

                    if( pScreen != NULL )
                    {
                        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
                        SECPtr pSec = SECPTR(pScrn);

                        pSec->is_fb_touched = TRUE;
                    }
                }
                op->buf[i].bo = NULL;
            }

            if (op->buf[i].pixmap)
            {
                op->buf[i].pixmap->devPrivate.ptr = NULL;
                op->buf[i].pixmap = NULL;
            }
            op->buf[i].addr = NULL;
        }

        privPixmap->exaOpInfo = NULL;
    }

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY)
        secLayerUpdate (secLayerFind (LAYER_OUTPUT_LCD, LAYER_UPPER));

    XDBG_TRACE (MEXAS, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr, op->refcnt);
}

void
_swDoDraw (struct xorg_list *l, DoDrawProcPtrEx do_draw, void* data)
{
    ExaBox *box = NULL;
    xorg_list_for_each_entry (box, l, link)
    {
        do_draw (box, data);
    }
}

static void
_swDoSolid (ExaBox* box, void* data)
{
    XDBG_TRACE (MEXAS, "(%d,%d), (%d,%d) off(%d,%d)\n",
                box->box.x1,
                box->box.y1,
                box->box.x2,
                box->box.y2,
                gOpSolid.x,
                gOpSolid.y);

    fbFill (&box->pDst->pixmap->drawable,
            gOpSolid.pGC,
            box->box.x1 + gOpSolid.x - box->pDst->pos.x1,
            box->box.y1 + gOpSolid.y - box->pDst->pos.y1,
            box->box.x2- box->box.x1,
            box->box.y2- box->box.y1);
}

static void
_swDoCopy (ExaBox* box, void* data)
{
    CARD8 alu = gOpCopy.alu;
    FbBits pm = gOpCopy.pm;
    FbBits *src;
    FbStride srcStride;
    int	srcBpp;
    FbBits *dst;
    FbStride dstStride;
    int	dstBpp;
    _X_UNUSED int	srcXoff, srcYoff;
    _X_UNUSED int	dstXoff, dstYoff;
    int srcX, srcY, dstX, dstY, width, height;

    XDBG_TRACE (MEXAS, "box(%d,%d),(%d,%d) src(%d,%d) dst(%d,%d)\n",
                box->box.x1,
                box->box.y1,
                box->box.x2,
                box->box.y2,
                gOpCopy.srcX,
                gOpCopy.srcY);

    srcX = gOpCopy.srcX + box->box.x1 - box->pSrc->pos.x1;
    srcY = gOpCopy.srcY + box->box.y1 - box->pSrc->pos.y1;
    dstX = gOpCopy.dstX + box->box.x1 - box->pDst->pos.x1;
    dstY = gOpCopy.dstY + box->box.y1 - box->pDst->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    fbGetDrawable (&box->pSrc->pixmap->drawable, src, srcStride, srcBpp, srcXoff, srcYoff);
    fbGetDrawable (&box->pDst->pixmap->drawable, dst, dstStride, dstBpp, dstXoff, dstYoff);
    /* temp fix : do right things later */
    if (!src || !dst)
    {
        return;
    }

    if (pm != FB_ALLONES ||
            alu != GXcopy ||
            gOpCopy.reverse ||
            gOpCopy.upsidedown ||
            !pixman_blt ((uint32_t *)src, (uint32_t *)dst,
                         srcStride,
                         dstStride,
                         srcBpp, dstBpp,
                         srcX, srcY, dstX, dstY, width, height))
    {
        fbBlt (src + srcY * srcStride,
               srcStride,
               srcX * srcBpp,

               dst + dstY * dstStride,
               dstStride,
               dstX * dstBpp,

               width * dstBpp,
               height,

               alu,
               pm,
               dstBpp,

               gOpCopy.reverse,
               gOpCopy.upsidedown);
    }
}

static void
_swDoComposite (ExaBox* box, void* data)
{
    PicturePtr	pDstPicture;
    pixman_image_t *src, *mask, *dest;
    int src_xoff, src_yoff, msk_xoff, msk_yoff;
    FbBits *bits;
    FbStride stride;
    int bpp;

    if (box->state == rgnPART)
    {
        XDBG_RETURN_IF_FAIL (gOpComposite.pSrcPicture->transform == NULL);
        XDBG_RETURN_IF_FAIL (gOpComposite.pMaskPicture &&
                             gOpComposite.pMaskPicture->transform == NULL);
    }

    pDstPicture = gOpComposite.pDstPicture;

    src = image_from_pict (gOpComposite.pSrcPicture, FALSE, &src_xoff, &src_yoff);
    mask = image_from_pict (gOpComposite.pMaskPicture, FALSE, &msk_xoff, &msk_yoff);

    fbGetPixmapBitsData (box->pDst->pixmap, bits, stride, bpp);
    dest = pixman_image_create_bits (pDstPicture->format,
                                     box->pDst->pixmap->drawable.width,
                                     box->pDst->pixmap->drawable.height,
                                     (uint32_t *)bits, stride * sizeof (FbStride));

    pixman_image_composite (gOpComposite.op,
                            src, mask, dest,
                            gOpComposite.srcX + box->box.x1,
                            gOpComposite.srcY + box->box.y1,
                            gOpComposite.maskX + box->box.x1,
                            gOpComposite.maskY + box->box.y1,
                            gOpComposite.dstX + box->box.x1 - box->pDst->pos.x1,
                            gOpComposite.dstY + box->box.y1 - box->pDst->pos.y1,
                            box->box.x2 - box->box.x1,
                            box->box.y2 - box->box.y1);

    free_pixman_pict (gOpComposite.pSrcPicture, src);
    free_pixman_pict (gOpComposite.pMaskPicture, mask);
    pixman_image_unref (dest);
}

static void
_swDoUploadToScreen (ExaBox* box, void* data)
{
    FbBits	*dst;
    FbStride	dstStride;
    int		dstBpp;
    _X_UNUSED int		dstXoff, dstYoff;
    int              srcStride;
    int             dstX, dstY;
    int             width, height;

    fbGetDrawable (&box->pDst->pixmap->drawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

    srcStride = gOpUTS.src_pitch/sizeof (uint32_t);
    dstX = gOpUTS.x + box->box.x1 - box->pDst->pos.x1;
    dstY = gOpUTS.y + box->box.y1 - box->pDst->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    XDBG_TRACE (MEXAS, "src(%p, %d) %d,%d,%d,%d\n",
                gOpUTS.src, srcStride, dstX, dstY, width, height);

    if(dstBpp < 8)
    {
        XDBG_WARNING(MEXAS, "dstBpp:%d\n", dstBpp);
        return;
    }

    if (!pixman_blt ((uint32_t *)gOpUTS.src,
                     (uint32_t *)dst,
                     srcStride,
                     dstStride,
                     dstBpp, dstBpp,
                     box->box.x1, box->box.y1,
                     dstX, dstY,
                     width, height))
    {
        unsigned char *pDst, *pSrc;
        int dst_pitch, src_pitch, cpp;

        pDst = (unsigned char*)dst;
        pSrc = (unsigned char*)gOpUTS.src;
        cpp = dstBpp / 8;
        src_pitch = gOpUTS.src_pitch;
        dst_pitch = box->pDst->pixmap->devKind;

        pSrc += box->box.y1 * src_pitch + box->box.x1 * cpp;
        pDst += dstY * dst_pitch + dstX * cpp;

        for (; height > 0; height--) {
            memcpy(pDst, pSrc, width * cpp);
            pDst += dst_pitch;
            pSrc += src_pitch;
        }
    }
}

static void
_swDoDownladFromScreen (ExaBox* box, void* data)
{
    FbBits	*src;
    FbStride	srcStride;
    int		srcBpp;
    _X_UNUSED int		srcXoff, srcYoff;
    int              dstStride;
    int             srcX, srcY;
    int             width, height;

    fbGetDrawable (&box->pSrc->pixmap->drawable, src, srcStride, srcBpp, srcXoff, srcYoff);

    dstStride = gOpDFS.dst_pitch/sizeof (uint32_t);
    srcX = gOpDFS.x + box->box.x1 - box->pSrc->pos.x1;
    srcY = gOpDFS.y + box->box.y1 - box->pSrc->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    XDBG_TRACE (MEXAS, "dst(%p, %d) %d,%d,%d,%d\n",
                gOpDFS.dst, dstStride, srcX, srcY, width, height);

    if(srcBpp < 8)
    {
        XDBG_WARNING(MEXAS, "srcBpp:%d\n", srcBpp);
        return;
    }

    if (!pixman_blt ((uint32_t *)src,
                     (uint32_t *)gOpDFS.dst,
                     srcStride,
                     dstStride,
                     srcBpp, srcBpp,
                     srcX, srcY,
                     box->box.x1, box->box.y1,
                     width, height))
    {
        unsigned char *pDst, *pSrc;
        int dst_pitch, src_pitch, cpp;

        pDst = (unsigned char*)gOpDFS.dst;
        pSrc = (unsigned char*)src;
        cpp = srcBpp / 8;
        src_pitch = box->pSrc->pixmap->devKind;
        dst_pitch = gOpDFS.dst_pitch;

        pSrc += srcY * src_pitch + srcX * cpp;
        pDst += box->box.y1 * dst_pitch + box->box.x1 * cpp;

        for (; height > 0; height--) {
            memcpy(pDst, pSrc, width * cpp);
            pDst += dst_pitch;
            pSrc += src_pitch;
        }
    }
}

static Bool
SECExaSwPrepareSolid (PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    ChangeGCVal tmpval[3];

    XDBG_TRACE (MEXAS, "\n");
    memset (&gOpSolid, 0x00, sizeof (gOpSolid));

    /* Put ff at the alpha bits when transparency is set to xv */
    if (pPixmap->drawable.depth == 24)
        fg = fg | (~ (pScrn->mask.red|pScrn->mask.green|pScrn->mask.blue));
    gOpSolid.alu = alu;
    gOpSolid.fg = fg;
    gOpSolid.planemask = planemask;
    gOpSolid.pixmap = pPixmap;

    gOpSolid.pOpDst = _swPrepareAccess (pPixmap, EXA_PREPARE_DEST);
    XDBG_GOTO_IF_FAIL (gOpSolid.pOpDst, bail);
    gOpSolid.pGC = GetScratchGC (pPixmap->drawable.depth, pScreen);

    tmpval[0].val = alu;
    tmpval[1].val = planemask;
    tmpval[2].val = fg;
    ChangeGC (NullClient, gOpSolid.pGC, GCFunction|GCPlaneMask|GCForeground, tmpval);
    ValidateGC (&pPixmap->drawable, gOpSolid.pGC);

    gOpSolid.bDo = TRUE;

    return TRUE;

bail:
    XDBG_TRACE (MEXAS, "FAIL: pix:%p hint:%d, num_pix:%d\n",
                pPixmap, index, pPixmap->usage_hint, gOpSolid.pOpDst->num);
    gOpSolid.bDo = FALSE;
    gOpSolid.pGC = NULL;

    return TRUE;
}


static void
SECExaSwSolid (PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    XDBG_TRACE (MEXAS, " (%d,%d), (%d,%d)\n", x1,y1,x2,y2);
    if (gOpSolid.bDo == FALSE) return;

    gOpSolid.x = x1;
    gOpSolid.y = y1;
    gOpSolid.w = x2-x1;
    gOpSolid.h = y2-y1;

    if (gOpSolid.pOpDst->isSame)
    {
        ExaBox box;

        box.state = rgnIN;
        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = x2-x1;
        box.box.y2 = y2-y1;
        box.pDst = &gOpSolid.pOpDst->buf[0];
        _swDoSolid (&box, NULL);
    }
    else
    {
        int i;
        ExaBox *box;
        BoxRec b;

        /*Init box list*/
        xorg_list_init (&gOpSolid.opBox);

        b.x1 = x1;
        b.y1 = y1;
        b.x2 = x2;
        b.y2 = y2;

        for (i=0; i<gOpSolid.pOpDst->num; i++)
        {
            box = _swBoxAdd (&gOpSolid.opBox,
                             &gOpSolid.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpSolid.pOpDst->buf[i];
            }
        }
        _swBoxMove (&gOpSolid.opBox, -x1, -y1);

        /* Call solid function */
        _swDoDraw (&gOpSolid.opBox,
                   _swDoSolid, NULL);

        /*Remove box list*/
        _swBoxRemoveAll (&gOpSolid.opBox);
    }
}

static void
SECExaSwDoneSolid (PixmapPtr pPixmap)
{
    XDBG_TRACE (MEXAS, "\n");
    if (gOpSolid.pGC)
    {
        FreeScratchGC (gOpSolid.pGC);
        gOpSolid.pGC = NULL;
    }

    if (gOpSolid.pixmap)
        _swFinishAccess (gOpSolid.pixmap, EXA_PREPARE_DEST);
}

static Bool
SECExaSwPrepareCopy (PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                     int dx, int dy, int alu, Pixel planemask)
{
    int num_dst_pix = -1;
    int num_src_pix = -1;

    XDBG_TRACE (MEXAS, "\n");
    memset (&gOpCopy, 0x00, sizeof (gOpCopy));

    gOpCopy.alu = alu;
    gOpCopy.pm = planemask;
    gOpCopy.reverse = (dx == 1)?0:1;
    gOpCopy.upsidedown = (dy == 1)?0:1;
    gOpCopy.pDstPix = pDstPixmap;
    gOpCopy.pSrcPix = pSrcPixmap;

    gOpCopy.pOpDst = _swPrepareAccess (pDstPixmap, EXA_PREPARE_DEST);
    XDBG_GOTO_IF_FAIL (gOpCopy.pOpDst, bail);
    gOpCopy.pOpSrc = _swPrepareAccess (pSrcPixmap, EXA_PREPARE_SRC);
    XDBG_GOTO_IF_FAIL (gOpCopy.pOpDst, bail);

    gOpCopy.bDo = TRUE;

    return TRUE;

bail:
    XDBG_TRACE (MEXAS, "FAIL\n");
    XDBG_TRACE (MEXAS, "   SRC pix:%p, index:%d, hint:%d, num_pix:%d\n",
                pSrcPixmap, index, pSrcPixmap->usage_hint, num_src_pix);
    XDBG_TRACE (MEXAS, "   DST pix:%p, index:%d, hint:%d, num_pix:%d\n",
                pDstPixmap, index, pDstPixmap->usage_hint, num_dst_pix);
    gOpCopy.bDo = FALSE;

    return TRUE;
}


static void
SECExaSwCopy (PixmapPtr pDstPixmap, int srcX, int srcY,
              int dstX, int dstY, int width, int height)
{
    XDBG_TRACE (MEXAS, "src(%d,%d) dst(%d,%d) %dx%d\n",
                srcX, srcY, dstX, dstY, width, height);

    if (gOpSolid.bDo == FALSE) return;

    gOpCopy.srcX = srcX;
    gOpCopy.srcY = srcY;
    gOpCopy.dstX = dstX;
    gOpCopy.dstY = dstY;
    gOpCopy.width = width;
    gOpCopy.height = height;

    if (gOpCopy.pOpSrc->isSame && gOpCopy.pOpDst->isSame)
    {
        ExaBox box;

        box.state = rgnIN;
        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = width;
        box.box.y2 = height;
        box.pDst = &gOpCopy.pOpDst->buf[0];
        box.pSrc = &gOpCopy.pOpSrc->buf[0];
        _swDoCopy (&box, NULL);
    }
    else
    {
        int i;
        struct xorg_list lSrc, lDst;
        ExaBox *box;
        BoxRec b;

        //Set Dest
        b.x1 = dstX;
        b.y1 = dstY;
        b.x2 = dstX + width;
        b.y2 = dstY + height;
        xorg_list_init (&lDst);
        for (i=0; i<gOpCopy.pOpDst->num; i++)
        {
            box = _swBoxAdd (&lDst,
                             &gOpCopy.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpCopy.pOpDst->buf[i];
            }
        }
        _swBoxMove (&lDst, -dstX, -dstY);

        //Set Src
        b.x1 = srcX;
        b.y1 = srcY;
        b.x2 = srcX + width;
        b.y2 = srcY + height;

        xorg_list_init (&lSrc);
        for (i=0; i<gOpCopy.pOpSrc->num; i++)
        {
            box = _swBoxAdd (&lSrc,
                             &gOpCopy.pOpSrc->buf[i].pos,
                             &b);
            if (box)
            {
                box->pSrc = &gOpCopy.pOpSrc->buf[i];
            }
        }
        _swBoxMove (&lSrc, -srcX, -srcY);

        //Merge and call copy
        xorg_list_init (&gOpCopy.opBox);
        _swBoxMerge (&gOpCopy.opBox, &lSrc, &lDst);
        _swDoDraw (&gOpCopy.opBox,
                   _swDoCopy, NULL);

        //Remove box list
        _swBoxRemoveAll (&lSrc);
        _swBoxRemoveAll (&lDst);
        _swBoxRemoveAll (&gOpCopy.opBox);
    }
}

static void
SECExaSwDoneCopy (PixmapPtr pDstPixmap)
{
    XDBG_TRACE (MEXAS, "\n");

    if (gOpCopy.pDstPix)
        _swFinishAccess (gOpCopy.pDstPix, EXA_PREPARE_DEST);
    if (gOpCopy.pSrcPix)
        _swFinishAccess (gOpCopy.pSrcPix, EXA_PREPARE_SRC);
}

static Bool
SECExaSwCheckComposite (int op, PicturePtr pSrcPicture,
                        PicturePtr pMaskPicture, PicturePtr pDstPicture)
{
    return TRUE;
}

static Bool
SECExaSwPrepareComposite (int op, PicturePtr pSrcPicture,
                          PicturePtr pMaskPicture, PicturePtr pDstPicture,
                          PixmapPtr pSrcPixmap,
                          PixmapPtr pMaskPixmap, PixmapPtr pDstPixmap)
{
    XDBG_TRACE (MEXAS, "\n");
    memset (&gOpComposite, 0x00, sizeof (gOpComposite));
    XDBG_GOTO_IF_FAIL (pDstPixmap != NULL, bail);

    gOpComposite.op = op;
    gOpComposite.pDstPicture = pDstPicture;
    gOpComposite.pSrcPicture = pSrcPicture;
    gOpComposite.pMaskPicture = pMaskPicture;
    gOpComposite.pSrcPixmap = pSrcPixmap;
    gOpComposite.pMaskPixmap = pMaskPixmap;
    gOpComposite.pDstPixmap = pDstPixmap;

    gOpComposite.pOpDst = _swPrepareAccess (pDstPixmap, EXA_PREPARE_DEST);

    if (pSrcPixmap)
    {
        gOpComposite.pOpSrc = _swPrepareAccess (pSrcPixmap, EXA_PREPARE_SRC);
        XDBG_GOTO_IF_FAIL (gOpComposite.pOpSrc->num == 1, bail);
    }

    if (pMaskPixmap)
    {
        gOpComposite.pOpMask = _swPrepareAccess (pMaskPixmap, EXA_PREPARE_MASK);
        XDBG_GOTO_IF_FAIL (gOpComposite.pOpMask->num == 1, bail);
    }

    gOpComposite.bDo = TRUE;

    return TRUE;

bail:
    XDBG_TRACE (MEXAS, "FAIL: op%d\n", op);
    XDBG_TRACE (MEXAS, "   SRC picture:%p pix:%p\n", pSrcPicture, pSrcPixmap);
    XDBG_TRACE (MEXAS, "   MASK picture:%p pix:%p\n", pMaskPicture, pMaskPixmap);
    XDBG_TRACE (MEXAS, "   DST picture:%p pix:%p\n", pDstPicture, pDstPixmap);

    gOpComposite.bDo = FALSE;

    return TRUE;
}

static void
SECExaSwComposite (PixmapPtr pDstPixmap, int srcX, int srcY,
                   int maskX, int maskY, int dstX, int dstY,
                   int width, int height)
{
    XDBG_TRACE (MEXAS, "s(%d,%d), m(%d,%d) d(%d,%d) %dx%d\n",
                srcX, srcY,
                maskX, maskY,
                dstX, dstY,
                width, height);
    if (!gOpComposite.bDo) return;

    gOpComposite.srcX = srcX;
    gOpComposite.srcY = srcY;
    gOpComposite.maskX = maskX;
    gOpComposite.maskY = maskY;
    gOpComposite.dstX = dstX;
    gOpComposite.dstY = dstY;
    gOpComposite.width = width;
    gOpComposite.height = height;

    if (gOpComposite.pOpDst->isSame)
    {
        ExaBox box;

        box.state = rgnIN;
        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = width;
        box.box.y2 = height;
        box.pDst = &gOpComposite.pOpDst->buf[0];
        box.pSrc = (gOpComposite.pOpSrc)? (&gOpComposite.pOpSrc->buf[0]):NULL;
        box.pSrc = (gOpComposite.pOpMask)? (&gOpComposite.pOpMask->buf[0]):NULL;

        _swDoComposite (&box, NULL);
    }
    else
    {
        int i;
        ExaBox *box;
        BoxRec b;

        /*Init box list*/
        xorg_list_init (&gOpComposite.opBox);

        b.x1 = dstX;
        b.y1 = dstY;
        b.x2 = dstX+width;
        b.y2 = dstY+height;

        for (i=0; i<gOpComposite.pOpDst->num; i++)
        {
            box = _swBoxAdd (&gOpComposite.opBox,
                             &gOpComposite.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpComposite.pOpDst->buf[i];
                box->pSrc = (gOpComposite.pOpSrc)? (&gOpComposite.pOpSrc->buf[0]):NULL;
                box->pMask= (gOpComposite.pOpMask)? (&gOpComposite.pOpMask->buf[0]):NULL;
            }
        }
        _swBoxMove (&gOpComposite.opBox, -dstX, -dstY);

        /* Call solid function */
        _swDoDraw (&gOpComposite.opBox,
                   _swDoComposite, NULL);

        /*Remove box list*/
        _swBoxRemoveAll (&gOpComposite.opBox);
    }
}

/* done composite : sw done composite, not using pvr2d */
static void
SECExaSwDoneComposite (PixmapPtr pDst)
{
    XDBG_TRACE (MEXAS, "\n");
    if (gOpComposite.pDstPixmap != NULL)
        _swFinishAccess (gOpComposite.pDstPixmap, EXA_PREPARE_DEST);
    if (gOpComposite.pSrcPixmap != NULL)
        _swFinishAccess (gOpComposite.pSrcPixmap, EXA_PREPARE_SRC);
    if (gOpComposite.pMaskPixmap != NULL)
        _swFinishAccess (gOpComposite.pMaskPixmap, EXA_PREPARE_MASK);
}

static Bool
SECExaSwUploadToScreen (PixmapPtr pDst, int x, int y, int w, int h,
                        char *src, int src_pitch)
{
    XDBG_RETURN_VAL_IF_FAIL (src!=NULL, TRUE);
    XDBG_TRACE (MEXAS, "src(%p, %d) %d,%d,%d,%d\n", src, src_pitch, x,y,w,h);
    XDBG_TRACE (MEXAS, "\tdst depth:%d, bpp:%d, pitch:%d, %dx%d\n",
                pDst->drawable.depth, pDst->drawable.bitsPerPixel, pDst->devKind,
                pDst->drawable.width, pDst->drawable.height);

    gOpUTS.pDst = pDst;
    gOpUTS.x = x;
    gOpUTS.y = y;
    gOpUTS.w = w;
    gOpUTS.h = h;
    gOpUTS.src = src;
    gOpUTS.src_pitch = src_pitch;
    gOpUTS.pOpDst = _swPrepareAccess (pDst, EXA_PREPARE_DEST);

    if (gOpUTS.pOpDst->isSame)
    {
        ExaBox box;

        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = w;
        box.box.y2 = h;
        box.state = rgnIN;
        box.pDst = &gOpUTS.pOpDst->buf[0];
        _swDoUploadToScreen (&box, NULL);
    }
    else
    {
        int i;
        ExaBox *box;
        BoxRec b;

        /*Init box list*/
        xorg_list_init (&gOpUTS.opBox);

        b.x1 = x;
        b.y1 = y;
        b.x2 = x+w;
        b.y2 = y+h;

        for (i=0; i<gOpUTS.pOpDst->num; i++)
        {
            box = _swBoxAdd (&gOpUTS.opBox,
                             &gOpUTS.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpUTS.pOpDst->buf[i];
            }
        }
        _swBoxMove (&gOpUTS.opBox, -x, -y);

        /* Call solid function */
        _swDoDraw (&gOpUTS.opBox,
                   _swDoUploadToScreen, NULL);

        /*Remove box list*/
        _swBoxRemoveAll (&gOpUTS.opBox);
    }

    _swFinishAccess (pDst, EXA_PREPARE_DEST);
    return TRUE;
}



static Bool
SECExaSwDownloadFromScreen (PixmapPtr pSrc, int x, int y, int w, int h,
                            char *dst, int dst_pitch)
{
    XDBG_RETURN_VAL_IF_FAIL (dst!=NULL, TRUE);
    XDBG_TRACE (MEXAS, "dst(%p, %d) %d,%d,%d,%d\n", dst, dst_pitch, x,y,w,h);

    gOpDFS.pSrc = pSrc;
    gOpDFS.x = x;
    gOpDFS.y = y;
    gOpDFS.w = w;
    gOpDFS.h = h;
    gOpDFS.dst = dst;
    gOpDFS.dst_pitch = dst_pitch;
    gOpDFS.pOpSrc = _swPrepareAccess (pSrc, EXA_PREPARE_SRC);

    if (gOpDFS.pOpSrc->isSame)
    {
        ExaBox box;

        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = w;
        box.box.y2 = h;
        box.state = rgnIN;
        box.pSrc = &gOpDFS.pOpSrc->buf[0];
        _swDoDownladFromScreen (&box, NULL);
    }
    else
    {
        int i;
        ExaBox *box;
        BoxRec b;

        /*Init box list*/
        xorg_list_init (&gOpDFS.opBox);

        b.x1 = x;
        b.y1 = y;
        b.x2 = x+w;
        b.y2 = y+h;

        for (i=0; i<gOpDFS.pOpSrc->num; i++)
        {
            box = _swBoxAdd (&gOpDFS.opBox,
                             &gOpDFS.pOpSrc->buf[i].pos,
                             &b);
            if (box)
            {
                box->pSrc = &gOpDFS.pOpSrc->buf[i];
            }
        }
        _swBoxMove (&gOpDFS.opBox, -x, -y);

        /* Call solid function */
        _swDoDraw (&gOpDFS.opBox,
                   _swDoDownladFromScreen, NULL);

        /*Remove box list*/
        _swBoxRemoveAll (&gOpDFS.opBox);
    }

    _swFinishAccess (pSrc, EXA_PREPARE_SRC);
    return TRUE;
}

int SECExaMarkSync(ScreenPtr pScreen)
{
    XDBG_RETURN_VAL_IF_FAIL (pScreen != NULL, TRUE);
    int ret=0;

    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR(pScrn);

    if( pSec && pSec->is_fb_touched == TRUE )
    {
        XDBG_TRACE(MEXAS, "UpdateRequest to the display!\n");

        ret = secDisplayUpdateRequest(pScrn);
        pSec->is_fb_touched = FALSE;
    }

    return ret;
}

Bool secExaSwInit (ScreenPtr pScreen, ExaDriverPtr pExaDriver)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    pExaDriver->PrepareSolid = SECExaSwPrepareSolid;
    pExaDriver->Solid = SECExaSwSolid;
    pExaDriver->DoneSolid = SECExaSwDoneSolid;

    pExaDriver->PrepareCopy = SECExaSwPrepareCopy;
    pExaDriver->Copy = SECExaSwCopy;
    pExaDriver->DoneCopy = SECExaSwDoneCopy;

    pExaDriver->CheckComposite = SECExaSwCheckComposite;
    pExaDriver->PrepareComposite = SECExaSwPrepareComposite;
    pExaDriver->Composite = SECExaSwComposite;
    pExaDriver->DoneComposite = SECExaSwDoneComposite;

    pExaDriver->UploadToScreen = SECExaSwUploadToScreen;
    pExaDriver->DownloadFromScreen = SECExaSwDownloadFromScreen;

    pExaDriver->MarkSync = SECExaMarkSync;

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "Succeed to Initialize SW EXA\n");

    return TRUE;
}

void secExaSwDeinit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "Succeed to finish SW EXA\n");
}
