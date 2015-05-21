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
#include "g2d/fimg2d.h"

#define DO(x)   ((x.bDo==DO_DRAW_NONE)?"SKIP": \
                            ((x.bDo==DO_DRAW_SW)?"SW":"HW"))

#define PIXINFO(pPixmap)       if(pPixmap) { \
                                                    XDBG_TRACE(MEXAH, "%s:%p(0x%x) %dx%d depth:%d(%d) pitch:%d\n", \
                                                                #pPixmap,    \
                                                                pPixmap, ID(pPixmap), \
                                                                pPixmap->drawable.width, pPixmap->drawable.height, \
                                                                pPixmap->drawable.depth, \
                                                                pPixmap->drawable.bitsPerPixel, \
                                                                pPixmap->devKind); \
                                                }

#define PICINFO(pPic)       if(pPic) { \
                                                    XDBG_TRACE(MEXAH, "%s, draw:%p, repeat:%d(%d), ca:%d, srcPict:%p\n", \
                                                                #pPic,    \
                                                                pPic->pDrawable,  \
                                                                pPic->repeat, pPic->repeatType, \
                                                                pPic->componentAlpha, \
                                                                pPic->pSourcePict); \
                                                    if(pPic->transform) { \
                                                        XDBG_TRACE("EXA2D", "\t0x%08x  0x%08x 0x%08x\n", \
                                                                    pPic->transform->matrix[0][0], \
                                                                    pPic->transform->matrix[0][1], \
                                                                    pPic->transform->matrix[0][2]); \
                                                        XDBG_TRACE("EXA2D", "\t0x%08x  0x%08x 0x%08x\n", \
                                                                    pPic->transform->matrix[1][0], \
                                                                    pPic->transform->matrix[1][1], \
                                                                    pPic->transform->matrix[1][2]); \
                                                        XDBG_TRACE("EXA2D", "\t0x%08x  0x%08x 0x%08x\n", \
                                                                    pPic->transform->matrix[1][0], \
                                                                    pPic->transform->matrix[1][1], \
                                                                    pPic->transform->matrix[1][2]); \
                                                    }\
                                                }

typedef struct
{
    BoxRec  pos;
    PixmapPtr pixmap;
    tbm_bo bo;

    unsigned int access_device; /*TBM_DEVICE_XXX*/
    unsigned int access_data;   /*pointer or gem*/
    G2dImage* imgG2d;
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

enum{
    DO_DRAW_NONE,
    DO_DRAW_SW,
    DO_DRAW_HW
};

typedef struct
{
    char bDo;

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
    char bDo;

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
    char bDo;

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

    char srcRepeat;
    char srcRotate;
    double srcScaleX;
    double srcScaleY;

    char maskRepeat;
    char maskRotate;
    double maskScaleX;
    double maskScaleY;

    ExaOpInfo* pOpSrc;
    ExaOpInfo* pOpMask;
    ExaOpInfo* pOpDst;
    struct xorg_list opBox;
} OpComposite;

typedef struct
{
    char bDo;

    PixmapPtr pDst;
    int x,y,w,h;
    char* src;
    int src_pitch;

    G2dImage* imgSrc;
    ExaOpInfo* pOpDst;
    struct xorg_list opBox;
} OpUTS;

typedef struct
{
    char bDo;

    PixmapPtr pSrc;
    int x,y,w,h;
    char* dst;
    int dst_pitch;

    G2dImage* imgDst;
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

ExaBox* _g2dBoxAdd (struct xorg_list *l, BoxPtr b1, BoxPtr b2)
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

void _g2dBoxMerge (struct xorg_list *l, struct xorg_list* b, struct xorg_list* t)
{
    ExaBox *b1 = NULL, *b2 = NULL;
    ExaBox* r = NULL;

    xorg_list_for_each_entry (b1, b, link)
    {
        xorg_list_for_each_entry (b2, t, link)
        {
            r = _g2dBoxAdd (l, &b1->box, &b2->box);
            if (r)
            {
                r->pSrc = b1->pSrc ? b1->pSrc : b2->pSrc;
                r->pMask= b1->pMask ? b1->pMask : b2->pMask;
                r->pDst = b1->pDst ? b1->pDst : b2->pDst;
            }
        }
    }
}

void _g2dBoxMove (struct xorg_list* l, int tx, int ty)
{
    ExaBox *b = NULL;

    xorg_list_for_each_entry (b, l, link)
    {
        secUtilBoxMove (&b->box, tx, ty);
    }
}

void _g2dBoxRemoveAll (struct xorg_list* l)
{
    ExaBox *ref = NULL, *next = NULL;

    xorg_list_for_each_entry_safe (ref, next, l, link)
    {
        xorg_list_del (&ref->link);
        free (ref);
    }
}

int _g2dBoxIsOne (struct xorg_list* l)
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

void _g2dBoxPrint (ExaBox* sb1, const char* name)
{
    ExaBox *b = NULL;

    xorg_list_for_each_entry (b, &sb1->link, link)
    {
        XDBG_DEBUG (MEXAS, "[%s] %d,%d - %d,%d\n", name,
                b->box.x1, b->box.y1, b->box.x2, b->box.y2);
    }
}

static pixman_bool_t
_g2d_check_within_epsilon (pixman_fixed_t a,
                pixman_fixed_t b,
                pixman_fixed_t epsilon)
{
    pixman_fixed_t t = a - b;

    if (t < 0)
        t = -t;

    return t <= epsilon;
}

static Bool
_g2d_check_picture(PicturePtr pPicture, char *rot90, double *scaleX, double *scaleY, char* repeat)
{
    struct pixman_transform* t;

#define EPSILON (pixman_fixed_t) (2)

#define IS_SAME(a, b) (_g2d_check_within_epsilon (a, b, EPSILON))
#define IS_ZERO(a)    (_g2d_check_within_epsilon (a, 0, EPSILON))
#define IS_ONE(a)     (_g2d_check_within_epsilon (a, F (1), EPSILON))
#define IS_UNIT(a)                          \
                                    (_g2d_check_within_epsilon (a, F (1), EPSILON) ||  \
                                    _g2d_check_within_epsilon (a, F (-1), EPSILON) || \
                                    IS_ZERO (a))
#define IS_INT(a)    (IS_ZERO (pixman_fixed_frac (a)))

/*RepeatNormal*/

    if(pPicture == NULL)
    {
        return TRUE;
    }

    if(pPicture->repeat)
    {
        switch(pPicture->repeatType)
        {
        case RepeatNormal:
            *repeat = G2D_REPEAT_MODE_REPEAT;
            break;
        case RepeatPad:
            *repeat = G2D_REPEAT_MODE_PAD;
            break;
        case RepeatReflect:
            *repeat = G2D_REPEAT_MODE_REFLECT;
            break;
        default:
            *repeat = G2D_REPEAT_MODE_NONE;
            break;
        }
    }
    else
    {
        *repeat = G2D_REPEAT_MODE_NONE;
    }

    if(pPicture->transform == NULL)
    {
        *rot90 = 0;
        *scaleX = 1.0;
        *scaleY = 1.0;
        return TRUE;
    }

    t= pPicture->transform;

    if(!IS_ZERO(t->matrix[0][0]) && IS_ZERO(t->matrix[0][1]) && IS_ZERO(t->matrix[1][0]) && !IS_ZERO(t->matrix[1][1]))
    {
        *rot90 = FALSE;
        *scaleX = pixman_fixed_to_double(t->matrix[0][0]);
        *scaleY = pixman_fixed_to_double(t->matrix[1][1]);
    }
    else if(IS_ZERO(t->matrix[0][0]) && !IS_ZERO(t->matrix[0][1]) && !IS_ZERO(t->matrix[1][0]) && IS_ZERO(t->matrix[1][1]))
    {
        /* FIMG2D 90 => PIXMAN 270 */
        *rot90 = TRUE;
        *scaleX = pixman_fixed_to_double(t->matrix[0][1]);
        *scaleY = pixman_fixed_to_double(t->matrix[1][0]*-1);
    }
    else
    {
        return FALSE;
    }

    return TRUE;
}

static Bool
_g2dIsSupport(PixmapPtr pPix, Bool forMask)
{
    SECPixmapPriv *privPixmap;

    if(!pPix) return TRUE;

    if(!forMask && pPix->drawable.depth < 8)
        return FALSE;

    privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPix);
    if(!privPixmap->isFrameBuffer && !privPixmap->bo)
        return FALSE;

    return TRUE;
}

static G2dImage*
_g2dGetImageFromPixmap(PixmapPtr pPix, unsigned int gem)
{
    G2dImage* img;
    G2dColorKeyMode mode;

    if(gem == 0)
    {
        gem = (unsigned int)pPix->devPrivate.ptr;
    }

    XDBG_RETURN_VAL_IF_FAIL((pPix != NULL && gem != 0), NULL);

    switch(pPix->drawable.depth)
    {
    case 32:
        mode = G2D_COLOR_FMT_ARGB8888|G2D_ORDER_AXRGB;
        break;
    case 24:
        mode = G2D_COLOR_FMT_XRGB8888|G2D_ORDER_AXRGB;
        break;
    case 16:
        mode = G2D_COLOR_FMT_RGB565|G2D_ORDER_AXRGB;
        break;
    case 8:
        mode = G2D_COLOR_FMT_A8|G2D_ORDER_AXRGB;
        break;
    case 1:
        mode = G2D_COLOR_FMT_A1|G2D_ORDER_AXRGB;
        break;
    default:
        XDBG_ERROR(MEXA, "Unsupport format depth:%d(%d),pitch:%d \n",
                   pPix->drawable.depth, pPix->drawable.bitsPerPixel, pPix->devKind);
        return NULL;
    }

    img = g2d_image_create_bo(mode,
                                            pPix->drawable.width,
                                            pPix->drawable.height,
                                            gem,
                                            pPix->devKind);

    return img;
}

static ExaOpInfo* _g2dPrepareAccess (PixmapPtr pPix, int index, unsigned int device)
{
    ScreenPtr pScreen = pPix->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);
    SECPixmapPriv *privPixmap = (SECPixmapPriv*)exaGetPixmapDriverPrivate (pPix);
    ExaOpInfo* op = &OpInfo[index];
    int opt = TBM_OPTION_READ;
    int i;
    tbm_bo *bos = NULL;
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
        XDBG_TRACE (MEXAH, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
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
        XDBG_TRACE (MEXAH,"FB  ret:%d num_pix:%d, %dx%d+%d+%d\n",
                    ret, num_bo,
                    pPix->drawable.width, pPix->drawable.height,
                    pPix->drawable.x, pPix->drawable.y);

        if (ret == rgnSAME && num_bo == 1)
        {
            op->num = 1;
            op->isSame = 1;

            op->buf[0].pixmap = pPix;
            op->buf[0].bo = bos[0];
            op->buf[0].pos.x1 = 0;
            op->buf[0].pos.y1 = 0;
            op->buf[0].pos.x2 = pPix->drawable.width;
            op->buf[0].pos.y2 = pPix->drawable.height;

            op->buf[0].access_device = device;
            bo_handle = tbm_bo_map (op->buf[0].bo, device, op->opt);
            op->buf[0].access_data = bo_handle.u32;
            op->buf[0].pixmap->devPrivate.ptr = (pointer)op->buf[0].access_data;
            if(device == TBM_DEVICE_2D)
            {
                op->buf[0].imgG2d = _g2dGetImageFromPixmap(op->buf[0].pixmap,
                                                                                            op->buf[0].access_data);
            }
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
                op->buf[i].pos = bo_data->pos;

                op->buf[i].access_device = device;
                bo_handle = tbm_bo_map (op->buf[i].bo, device, op->opt);
                op->buf[i].access_data = bo_handle.u32;
                op->buf[i].pixmap->devPrivate.ptr = (pointer)op->buf[i].access_data;
                if(device == TBM_DEVICE_2D)
                {
                    op->buf[i].imgG2d = _g2dGetImageFromPixmap(op->buf[i].pixmap,
                                                                                                op->buf[i].access_data);
                }
            }
        }
        if (bos)
            free (bos);
    }
    else
    {
        op->num = 1;
        op->isSame = 1;

        op->buf[0].pixmap = pPix;
        op->buf[0].bo = privPixmap->bo;
        op->buf[0].pos.x1 = 0;
        op->buf[0].pos.y1 = 0;
        op->buf[0].pos.x2 = pPix->drawable.width;
        op->buf[0].pos.y2 = pPix->drawable.height;

        op->buf[0].access_device = device;
        if (privPixmap->bo)
        {
            bo_handle = tbm_bo_map (op->buf[0].bo, device, op->opt);
            op->buf[0].access_data =  bo_handle.u32;
            if(device == TBM_DEVICE_2D)
            {
                op->buf[0].imgG2d = _g2dGetImageFromPixmap(op->buf[0].pixmap, op->buf[0].access_data);
            }
        }
        else
        {
            op->buf[0].access_data = (unsigned int)privPixmap->pPixData;
            op->buf[0].imgG2d = NULL;
        }
        op->buf[0].pixmap->devPrivate.ptr = (pointer)op->buf[0].access_data;
    }

    privPixmap->exaOpInfo = op;

    XDBG_TRACE (MEXAH, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr, op->refcnt);
    return op;
}

static void _g2dFinishAccess (PixmapPtr pPix, int index)
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
        for (i=0; i<op->num; i++)
        {
            if(op->buf[i].bo)
            {
                tbm_bo_unmap(op->buf[i].bo);
                op->buf[i].bo = NULL;
            }

            if(op->buf[i].pixmap)
            {
                op->buf[i].pixmap->devPrivate.ptr = NULL;
                op->buf[i].pixmap = NULL;
            }

            if(op->buf[i].imgG2d)
            {
                g2d_image_free(op->buf[i].imgG2d);
                op->buf[i].imgG2d = NULL;
            }

            op->buf[i].access_data = (unsigned int)NULL;
        }

        privPixmap->exaOpInfo = NULL;
    }

    if (pPix->usage_hint == CREATE_PIXMAP_USAGE_OVERLAY)
        secLayerUpdate (secLayerFind (LAYER_OUTPUT_LCD, LAYER_UPPER));

    XDBG_TRACE (MEXAH, "pix:%p index:%d hint:%d ptr:%p ref:%d\n",
                pPix, index, pPix->usage_hint, pPix->devPrivate.ptr, op->refcnt);
}

void
_g2dDoDraw (struct xorg_list *l, DoDrawProcPtrEx do_draw, void* data)
{
    ExaBox *box = NULL;
    xorg_list_for_each_entry (box, l, link)
    {
        do_draw (box, data);
    }
}

static void
_g2dDoSolid (ExaBox* box, void* data)
{
    XDBG_TRACE (MEXAH, "[%s] (%d,%d), (%d,%d) off(%d,%d)\n",
                DO(gOpSolid),
                box->box.x1,
                box->box.y1,
                box->box.x2,
                box->box.y2,
                gOpSolid.x,
                gOpSolid.y);

    if(gOpSolid.bDo == DO_DRAW_SW)
    {
        fbFill (&box->pDst->pixmap->drawable,
                gOpSolid.pGC,
                box->box.x1 + gOpSolid.x - box->pDst->pos.x1,
                box->box.y1 + gOpSolid.y - box->pDst->pos.y1,
                box->box.x2- box->box.x1,
                box->box.y2- box->box.y1);
    }
    else
    {
        util_g2d_fill_alu(box->pDst->imgG2d,
                box->box.x1 + gOpSolid.x - box->pDst->pos.x1,
                box->box.y1 + gOpSolid.y - box->pDst->pos.y1,
                box->box.x2- box->box.x1,
                box->box.y2- box->box.y1,
                gOpSolid.fg,
                (G2dAlu)gOpSolid.alu);
    }
}

static void
_g2dDoCopy (ExaBox* box, void* data)
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

    XDBG_TRACE (MEXAH, "[%s] box(%d,%d),(%d,%d) src(%d,%d) dst(%d,%d)\n",
                DO(gOpCopy),
                box->box.x1,
                box->box.y1,
                box->box.x2,
                box->box.y2,
                gOpCopy.srcX,
                gOpCopy.srcY,
                gOpCopy.dstX,
                gOpCopy.dstY);

    srcX = gOpCopy.srcX + box->box.x1 - box->pSrc->pos.x1;
    srcY = gOpCopy.srcY + box->box.y1 - box->pSrc->pos.y1;
    dstX = gOpCopy.dstX + box->box.x1 - box->pDst->pos.x1;
    dstY = gOpCopy.dstY + box->box.y1 - box->pDst->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    if(gOpCopy.bDo == DO_DRAW_SW)
    {
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
    else
    {
        util_g2d_copy_alu(box->pSrc->imgG2d,
                                        box->pDst->imgG2d,
                                        srcX, srcY,
                                        dstX, dstY,
                                        width, height,
                                        gOpCopy.alu);
    }
}

static void
_g2dDoComposite (ExaBox* box, void* data)
{

    if (box->state == rgnPART)
    {
        XDBG_RETURN_IF_FAIL (gOpComposite.pSrcPicture->transform == NULL);
        XDBG_RETURN_IF_FAIL (gOpComposite.pMaskPicture &&
                             gOpComposite.pMaskPicture->transform == NULL);
    }

    if (gOpComposite.bDo == DO_DRAW_SW)
    {
        PicturePtr	pDstPicture;
        pixman_image_t *src, *mask, *dest;
        int src_xoff, src_yoff, msk_xoff, msk_yoff;
        FbBits *bits;
        FbStride stride;
        int bpp;

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
    else
    {
        util_g2d_composite (gOpComposite.op,
                                    box->pSrc->imgG2d,
                                    box->pMask ? box->pMask->imgG2d:NULL,
                                    box->pDst->imgG2d,
                                    gOpComposite.srcX + box->box.x1,
                                    gOpComposite.srcY + box->box.y1,
                                    gOpComposite.maskX + box->box.x1,
                                    gOpComposite.maskY + box->box.y1,
                                    gOpComposite.dstX + box->box.x1 - box->pDst->pos.x1,
                                    gOpComposite.dstY + box->box.y1 - box->pDst->pos.y1,
                                    box->box.x2 - box->box.x1,
                                    box->box.y2 - box->box.y1);
    }
}

static void
_g2dDoUploadToScreen (ExaBox* box, void* data)
{
    int             dstX, dstY;
    int             width, height;

    dstX = gOpUTS.x + box->box.x1 - box->pDst->pos.x1;
    dstY = gOpUTS.y + box->box.y1 - box->pDst->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    if(gOpUTS.bDo == DO_DRAW_SW)
    {
        FbBits	*dst;
        FbStride	dstStride;
        int		dstBpp;
        _X_UNUSED int		dstXoff, dstYoff;
        int              srcStride;

        fbGetDrawable (&box->pDst->pixmap->drawable, dst, dstStride, dstBpp, dstXoff, dstYoff);

        srcStride = gOpUTS.src_pitch/sizeof (uint32_t);

        XDBG_TRACE (MEXAH, "src(%p, %d) %d,%d,%d,%d\n",
                    gOpUTS.src, srcStride, dstX, dstY, width, height);

        if (!pixman_blt ((uint32_t *)gOpUTS.src,
                         (uint32_t *)dst,
                         srcStride,
                         dstStride,
                         dstBpp, dstBpp,
                         box->box.x1, box->box.y1,
                         dstX, dstY,
                         width, height))
        {
            fbBlt ((FbBits*) ((FbBits*)gOpUTS.src),
                   srcStride,
                   box->box.x1,

                   dst + dstY * dstStride,
                   dstStride,
                   dstX,

                   width,
                   height,

                   GXcopy,
                   ~0,
                   dstBpp,

                   0,
                   0);
        }
    }
    else
    {
        util_g2d_copy(gOpUTS.imgSrc,
                            box->pDst->imgG2d,
                            box->box.x1, box->box.y1,
                            dstX, dstY,
                            width, height);
    }
}

static void
_g2dDoDownladFromScreen (ExaBox* box, void* data)
{
    int             srcX, srcY;
    int             width, height;
    int             dstStride;

    dstStride = gOpDFS.dst_pitch/sizeof (uint32_t);
    srcX = gOpDFS.x + box->box.x1 - box->pSrc->pos.x1;
    srcY = gOpDFS.y + box->box.y1 - box->pSrc->pos.y1;
    width = box->box.x2 - box->box.x1;
    height = box->box.y2 - box->box.y1;

    XDBG_TRACE (MEXAH, "dst(%p, %d) %d,%d,%d,%d\n",
                gOpDFS.dst, dstStride, srcX, srcY, width, height);

    if(gOpDFS.bDo == DO_DRAW_SW)
    {
        FbBits	*src;
        FbStride	srcStride;
        int		srcBpp;
        _X_UNUSED int		srcXoff, srcYoff;

        fbGetDrawable (&box->pSrc->pixmap->drawable, src, srcStride, srcBpp, srcXoff, srcYoff);

        if (!pixman_blt ((uint32_t *)src,
                         (uint32_t *)gOpDFS.dst,
                         srcStride,
                         dstStride,
                         srcBpp, srcBpp,
                         srcX, srcY,
                         box->box.x1, box->box.y1,
                         width, height))
        {
            fbBlt (src + srcY * srcStride,
                   srcStride,
                   srcX,

                   (FbBits*) ((FbBits*)gOpDFS.dst),
                   dstStride,
                   box->box.x1,

                   width,
                   height,

                   GXcopy,
                   ~0,
                   srcBpp,

                   0,
                   0);
        }
    }
    else
    {
        util_g2d_copy (box->pSrc->imgG2d,
                                gOpDFS.imgDst,
                                srcX, srcY,
                                box->box.x1, box->box.y1,
                                width, height);
    }
}

static Bool
SECExaG2dPrepareSolid (PixmapPtr pPixmap, int alu, Pixel planemask, Pixel fg)
{
    ScreenPtr pScreen = pPixmap->drawable.pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    ChangeGCVal tmpval[3];

    XDBG_TRACE (MEXAH, "\n");
    memset (&gOpSolid, 0x00, sizeof (gOpSolid));

    /* Put ff at the alpha bits when transparency is set to xv */
    if (pPixmap->drawable.depth == 24)
        fg = fg | (~ (pScrn->mask.red|pScrn->mask.green|pScrn->mask.blue));

    gOpSolid.alu = alu;
    gOpSolid.fg = fg;
    gOpSolid.planemask = planemask;
    gOpSolid.pixmap = pPixmap;

    if (!_g2dIsSupport(pPixmap, 0))
    {
        gOpSolid.pOpDst = _g2dPrepareAccess (pPixmap,
                                                                        EXA_PREPARE_DEST,
                                                                        TBM_DEVICE_CPU);
        XDBG_GOTO_IF_FAIL (gOpSolid.pOpDst, bail);

        gOpSolid.pGC = GetScratchGC (pPixmap->drawable.depth, pScreen);
        tmpval[0].val = alu;
        tmpval[1].val = planemask;
        tmpval[2].val = fg;
        ChangeGC (NullClient, gOpSolid.pGC, GCFunction|GCPlaneMask|GCForeground, tmpval);
        ValidateGC (&pPixmap->drawable, gOpSolid.pGC);

        gOpSolid.bDo = DO_DRAW_SW;
    }
    else
    {
        gOpSolid.pOpDst = _g2dPrepareAccess (pPixmap,
                                                                        EXA_PREPARE_DEST,
                                                                        TBM_DEVICE_2D);
        XDBG_GOTO_IF_FAIL (gOpSolid.pOpDst, bail);
        gOpSolid.bDo = DO_DRAW_HW;
    }

    return TRUE;

bail:
    XDBG_TRACE (MEXAH, "FAIL: pix:%p hint:%d, num_pix:%d\n",
                pPixmap, pPixmap->usage_hint, gOpSolid.pOpDst->num);
    gOpSolid.bDo = DO_DRAW_NONE;
    gOpSolid.pGC = NULL;

    return TRUE;
}


static void
SECExaG2dSolid (PixmapPtr pPixmap, int x1, int y1, int x2, int y2)
{
    XDBG_TRACE (MEXAH, " (%d,%d), (%d,%d)\n", x1,y1,x2,y2);
    XDBG_TRACE (MEXAH, "%s\n", DO(gOpSolid));
    if (gOpSolid.bDo == DO_DRAW_NONE) return;

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
        _g2dDoSolid (&box, NULL);
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
            box = _g2dBoxAdd (&gOpSolid.opBox,
                             &gOpSolid.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpSolid.pOpDst->buf[i];
            }
        }
        _g2dBoxMove (&gOpSolid.opBox, -x1, -y1);

        /* Call solid function */
        _g2dDoDraw (&gOpSolid.opBox,
                   _g2dDoSolid, NULL);

        /*Remove box list*/
        _g2dBoxRemoveAll (&gOpSolid.opBox);
    }
}

static void
SECExaG2dDoneSolid (PixmapPtr pPixmap)
{
    XDBG_TRACE (MEXAH, "\n");

    if (gOpSolid.bDo)
        g2d_exec();

    if (gOpSolid.pGC)
    {
        FreeScratchGC (gOpSolid.pGC);
        gOpSolid.pGC = NULL;
    }

    if (gOpSolid.pixmap)
        _g2dFinishAccess (gOpSolid.pixmap, EXA_PREPARE_DEST);
}

static Bool
SECExaG2dPrepareCopy (PixmapPtr pSrcPixmap, PixmapPtr pDstPixmap,
                     int dx, int dy, int alu, Pixel planemask)
{
    int num_dst_pix = -1;
    int num_src_pix = -1;
    unsigned int draw_type = DO_DRAW_HW;
    unsigned int access_device = TBM_DEVICE_2D;

    XDBG_TRACE (MEXAH, "\n");
    memset (&gOpCopy, 0x00, sizeof (gOpCopy));

    gOpCopy.alu = alu;
    gOpCopy.pm = planemask;
    gOpCopy.reverse = (dx == 1)?0:1;
    gOpCopy.upsidedown = (dy == 1)?0:1;
    gOpCopy.pDstPix = pDstPixmap;
    gOpCopy.pSrcPix = pSrcPixmap;

    /* Check capability */
    if(!_g2dIsSupport(pSrcPixmap, 0) ||
        !_g2dIsSupport(pDstPixmap, 0) ||
        gOpCopy.reverse ||
        gOpCopy.upsidedown)
    {
        draw_type = DO_DRAW_SW;
        access_device = TBM_DEVICE_CPU;
    }

    gOpCopy.pOpDst = _g2dPrepareAccess (pDstPixmap, EXA_PREPARE_DEST, access_device);
    XDBG_GOTO_IF_FAIL (gOpCopy.pOpDst, bail);
    gOpCopy.pOpSrc = _g2dPrepareAccess (pSrcPixmap, EXA_PREPARE_SRC, access_device);
    XDBG_GOTO_IF_FAIL (gOpCopy.pOpDst, bail);

    gOpCopy.bDo = draw_type;

    return TRUE;

bail:
    XDBG_TRACE (MEXAH, "FAIL\n");
    XDBG_TRACE (MEXAH, "   SRC pix:%p, hint:%d, num_pix:%d\n",
                pSrcPixmap, pSrcPixmap->usage_hint, num_src_pix);
    XDBG_TRACE (MEXAH, "   DST pix:%p, hint:%d, num_pix:%d\n",
                pDstPixmap, pDstPixmap->usage_hint, num_dst_pix);
    gOpCopy.bDo = DO_DRAW_NONE;

    return TRUE;
}


static void
SECExaG2dCopy (PixmapPtr pDstPixmap, int srcX, int srcY,
              int dstX, int dstY, int width, int height)
{
    XDBG_TRACE (MEXAH, "%s\n", DO(gOpCopy));
    XDBG_TRACE (MEXAH, "src(%d,%d) dst(%d,%d) %dx%d\n",
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
        _g2dDoCopy (&box, NULL);
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
            box = _g2dBoxAdd (&lDst,
                             &gOpCopy.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpCopy.pOpDst->buf[i];
            }
        }
        _g2dBoxMove (&lDst, -dstX, -dstY);

        //Set Src
        b.x1 = srcX;
        b.y1 = srcY;
        b.x2 = srcX + width;
        b.y2 = srcY + height;

        xorg_list_init (&lSrc);
        for (i=0; i<gOpCopy.pOpSrc->num; i++)
        {
            box = _g2dBoxAdd (&lSrc,
                             &gOpCopy.pOpSrc->buf[i].pos,
                             &b);
            if (box)
            {
                box->pSrc = &gOpCopy.pOpSrc->buf[i];
            }
        }
        _g2dBoxMove (&lSrc, -srcX, -srcY);

        //Merge and call copy
        xorg_list_init (&gOpCopy.opBox);
        _g2dBoxMerge (&gOpCopy.opBox, &lSrc, &lDst);
        _g2dDoDraw (&gOpCopy.opBox,
                   _g2dDoCopy, NULL);

        //Remove box list
        _g2dBoxRemoveAll (&lSrc);
        _g2dBoxRemoveAll (&lDst);
        _g2dBoxRemoveAll (&gOpCopy.opBox);
    }
}

static void
SECExaG2dDoneCopy (PixmapPtr pDstPixmap)
{
    XDBG_TRACE (MEXAH, "\n");
    if (gOpCopy.bDo == DO_DRAW_HW)
        g2d_exec();

    if (gOpCopy.pDstPix)
        _g2dFinishAccess (gOpCopy.pDstPix, EXA_PREPARE_DEST);
    if (gOpCopy.pSrcPix)
        _g2dFinishAccess (gOpCopy.pSrcPix, EXA_PREPARE_SRC);
}

static Bool
SECExaG2dCheckComposite (int op, PicturePtr pSrcPicture,
                        PicturePtr pMaskPicture, PicturePtr pDstPicture)
{
    return TRUE;
}

static Bool
SECExaG2dPrepareComposite (int op, PicturePtr pSrcPicture,
                          PicturePtr pMaskPicture, PicturePtr pDstPicture,
                          PixmapPtr pSrcPixmap,
                          PixmapPtr pMaskPixmap, PixmapPtr pDstPixmap)
{
    XDBG_GOTO_IF_FAIL (pDstPixmap, bail);
    XDBG_GOTO_IF_FAIL (pSrcPicture && pDstPicture, bail);

    unsigned int draw_type = DO_DRAW_HW;
    unsigned int access_device = TBM_DEVICE_2D;

    XDBG_TRACE (MEXAH, "\n");
    memset (&gOpComposite, 0x00, sizeof (gOpComposite));

    gOpComposite.op = op;
    gOpComposite.pDstPicture = pDstPicture;
    gOpComposite.pSrcPicture = pSrcPicture;
    gOpComposite.pMaskPicture = pMaskPicture;
    gOpComposite.pSrcPixmap = pSrcPixmap;
    gOpComposite.pMaskPixmap = pMaskPixmap;
    gOpComposite.pDstPixmap = pDstPixmap;

    if (!_g2dIsSupport(pSrcPixmap, 0) ||
        !_g2dIsSupport(pDstPixmap, 0) ||
        !_g2dIsSupport(pMaskPixmap, 1))
    {
        draw_type = DO_DRAW_SW;
    }

    if (!_g2d_check_picture(pSrcPicture,
                                    &gOpComposite.srcRotate,
                                    &gOpComposite.srcScaleX,
                                    &gOpComposite.srcScaleY,
                                    &gOpComposite.srcRepeat) ||
         !_g2d_check_picture(pMaskPicture,
                                    &gOpComposite.maskRotate,
                                    &gOpComposite.maskScaleX,
                                    &gOpComposite.maskScaleY,
                                    &gOpComposite.maskRepeat))
    {
        draw_type = DO_DRAW_SW;
    }

    if(draw_type == DO_DRAW_SW)
    {
        access_device = TBM_DEVICE_CPU;
    }

    gOpComposite.pOpDst = _g2dPrepareAccess (pDstPixmap,
                                                                            EXA_PREPARE_DEST,
                                                                            access_device);

    if (pSrcPixmap)
    {
        gOpComposite.pOpSrc = _g2dPrepareAccess (pSrcPixmap,
                                                                                EXA_PREPARE_SRC,
                                                                                access_device);
        XDBG_GOTO_IF_FAIL (gOpComposite.pOpSrc->num == 1, bail);
    }

    if (pMaskPixmap)
    {
        gOpComposite.pOpMask = _g2dPrepareAccess (pMaskPixmap,
                                                                                    EXA_PREPARE_MASK,
                                                                                    access_device);
        XDBG_GOTO_IF_FAIL (gOpComposite.pOpMask->num == 1, bail);
    }

    if(draw_type == DO_DRAW_HW)
    {
        G2dImage *imgSrc = NULL, *imgMask = NULL;

        if(pSrcPicture)
        {
            if(gOpComposite.pOpSrc == NULL)
            {
                gOpComposite.pOpSrc = &OpInfo[EXA_PREPARE_SRC];
                gOpComposite.pOpSrc->buf[0].imgG2d =
                            g2d_image_create_solid((unsigned int)gOpComposite.pSrcPicture->pSourcePict->solidFill.color);
            }

            imgSrc = gOpComposite.pOpSrc->buf[0].imgG2d;
        }


        if(pMaskPicture)
        {
            if(gOpComposite.pOpMask == NULL)
            {
                gOpComposite.pOpMask = &OpInfo[EXA_PREPARE_MASK];
                gOpComposite.pOpMask->buf[0].imgG2d =
                            g2d_image_create_solid((unsigned int)gOpComposite.pSrcPicture->pSourcePict->solidFill.color);
            }

            imgMask = gOpComposite.pOpMask->buf[0].imgG2d;
        }

        /*Set Repeat*/
        imgSrc->repeat_mode = gOpComposite.srcRepeat;

        /*Set Rotate */
        imgSrc->rotate_90 = gOpComposite.srcRotate;
        imgSrc->xDir = (gOpComposite.srcScaleX < 0.0);
        imgSrc->yDir = (gOpComposite.srcScaleY < 0.0);

        /*Set Scale*/
        if(((gOpComposite.srcScaleX != 1.0 && gOpComposite.srcScaleX != -1.0) ||
            (gOpComposite.srcScaleY != 1.0 && gOpComposite.srcScaleY != -1.0)))
        {
            imgSrc->xscale = G2D_DOUBLE_TO_FIXED(gOpComposite.srcScaleX);
            imgSrc->yscale = G2D_DOUBLE_TO_FIXED(gOpComposite.srcScaleY);
            imgSrc->scale_mode = G2D_SCALE_MODE_BILINEAR;
        }

        if(imgMask)
        {
            /*Set Repeat*/
            imgMask->repeat_mode = gOpComposite.maskRepeat;

            /*Set Rotate */
            imgMask->rotate_90 = gOpComposite.maskRotate;
            imgMask->xDir = (gOpComposite.maskScaleX < 0.0);
            imgMask->yDir = (gOpComposite.maskScaleY < 0.0);

            /*Set Scale*/
            if(((gOpComposite.maskScaleX != 1.0 && gOpComposite.maskScaleX != -1.0) ||
                (gOpComposite.maskScaleY != 1.0 && gOpComposite.maskScaleY != -1.0)))
            {
                imgMask->xscale = G2D_DOUBLE_TO_FIXED(gOpComposite.maskScaleX);
                imgMask->yscale = G2D_DOUBLE_TO_FIXED(gOpComposite.maskScaleY);
                imgMask->scale_mode = G2D_SCALE_MODE_BILINEAR;
            }
        }
    }

    gOpComposite.bDo = draw_type;

    return TRUE;

bail:
    XDBG_TRACE (MEXAH, "FAIL: op%d\n", op);
    XDBG_TRACE (MEXAH, "   SRC picture:%p pix:%p\n", pSrcPicture, pSrcPixmap);
    XDBG_TRACE (MEXAH, "   MASK picture:%p pix:%p\n", pMaskPicture, pMaskPixmap);
    XDBG_TRACE (MEXAH, "   DST picture:%p pix:%p\n", pDstPicture, pDstPixmap);

    gOpComposite.bDo = DO_DRAW_NONE;

    return TRUE;
}

static void
SECExaG2dComposite (PixmapPtr pDstPixmap, int srcX, int srcY,
                   int maskX, int maskY, int dstX, int dstY,
                   int width, int height)
{
    XDBG_TRACE (MEXAH, "%s\n", DO(gOpComposite));
    XDBG_TRACE (MEXAH, "s(%d,%d), m(%d,%d) d(%d,%d) %dx%d\n",
                srcX, srcY,
                maskX, maskY,
                dstX, dstY,
                width, height);
    if (gOpComposite.bDo == DO_DRAW_NONE) return;

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
        box.pSrc = &gOpComposite.pOpSrc->buf[0];
        box.pMask = (gOpComposite.pOpMask)? (&gOpComposite.pOpMask->buf[0]):NULL;

        _g2dDoComposite (&box, NULL);
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
            box = _g2dBoxAdd (&gOpComposite.opBox,
                             &gOpComposite.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpComposite.pOpDst->buf[i];
                box->pSrc = &gOpComposite.pOpSrc->buf[0];
                box->pMask= (gOpComposite.pOpMask)? (&gOpComposite.pOpMask->buf[0]):NULL;
            }
        }
        _g2dBoxMove (&gOpComposite.opBox, -dstX, -dstY);

        /* Call solid function */
        _g2dDoDraw (&gOpComposite.opBox,
                   _g2dDoComposite, NULL);

        /*Remove box list*/
        _g2dBoxRemoveAll (&gOpComposite.opBox);
    }
}

/* done composite : sw done composite, not using pvr2d */
static void
SECExaG2dDoneComposite (PixmapPtr pDst)
{
    XDBG_TRACE (MEXAH, "\n");

    if(gOpComposite.bDo == DO_DRAW_HW)
        g2d_exec();

    if (gOpComposite.pDstPixmap != NULL)
        _g2dFinishAccess (gOpComposite.pDstPixmap, EXA_PREPARE_DEST);

    if (gOpComposite.pSrcPixmap != NULL)
        _g2dFinishAccess (gOpComposite.pSrcPixmap, EXA_PREPARE_SRC);
    else if (gOpComposite.pOpSrc)
    {
        g2d_image_free (gOpComposite.pOpSrc->buf[0].imgG2d);
        gOpComposite.pOpSrc->buf[0].imgG2d = NULL;
    }

    if (gOpComposite.pMaskPixmap != NULL)
        _g2dFinishAccess (gOpComposite.pMaskPixmap, EXA_PREPARE_MASK);
    else if (gOpComposite.pOpMask != NULL)
    {
        g2d_image_free (gOpComposite.pOpMask->buf[0].imgG2d);
        gOpComposite.pOpMask->buf[0].imgG2d = NULL;
    }
}

static Bool
SECExaG2dUploadToScreen (PixmapPtr pDst, int x, int y, int w, int h,
                        char *src, int src_pitch)
{
    XDBG_RETURN_VAL_IF_FAIL (src!=NULL, TRUE);
    XDBG_TRACE (MEXAH, "src(%p, %d) %d,%d,%d,%d\n", src, src_pitch, x,y,w,h);

    gOpUTS.pDst = pDst;
    gOpUTS.x = x;
    gOpUTS.y = y;
    gOpUTS.w = w;
    gOpUTS.h = h;
    gOpUTS.src = src;
    gOpUTS.src_pitch = src_pitch;

    if(_g2dIsSupport(pDst, FALSE))
    {
        gOpUTS.pOpDst = _g2dPrepareAccess (pDst,
                                                                    EXA_PREPARE_DEST,
                                                                    TBM_DEVICE_2D);
        gOpUTS.imgSrc = g2d_image_create_data (gOpUTS.pOpDst->buf[0].imgG2d->color_mode,
                                                                    w, h, (void*)src, src_pitch);
        gOpUTS.bDo = DO_DRAW_HW;
    }
    else
    {
        gOpUTS.pOpDst = _g2dPrepareAccess (pDst,
                                                                    EXA_PREPARE_DEST,
                                                                    TBM_DEVICE_CPU);
        gOpUTS.imgSrc = NULL;
        gOpUTS.bDo = DO_DRAW_SW;
    }

    XDBG_TRACE (MEXAH, "%s\n", DO(gOpUTS));
    if (gOpUTS.pOpDst->isSame)
    {
        ExaBox box;

        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = w;
        box.box.y2 = h;
        box.state = rgnIN;
        box.pDst = &gOpUTS.pOpDst->buf[0];
        _g2dDoUploadToScreen (&box, NULL);
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
            box = _g2dBoxAdd (&gOpUTS.opBox,
                             &gOpUTS.pOpDst->buf[i].pos,
                             &b);
            if (box)
            {
                box->pDst = &gOpUTS.pOpDst->buf[i];
            }
        }
        _g2dBoxMove (&gOpUTS.opBox, -x, -y);

        /* Call solid function */
        _g2dDoDraw (&gOpUTS.opBox,
                   _g2dDoUploadToScreen, NULL);

        /*Remove box list*/
        _g2dBoxRemoveAll (&gOpUTS.opBox);
    }

    if(gOpUTS.bDo == DO_DRAW_HW)
        g2d_exec();

    _g2dFinishAccess (pDst, EXA_PREPARE_DEST);
    if(gOpUTS.imgSrc)
    {
        g2d_image_free(gOpUTS.imgSrc);
    }
    return TRUE;
}



static Bool
SECExaG2dDownloadFromScreen (PixmapPtr pSrc, int x, int y, int w, int h,
                            char *dst, int dst_pitch)
{
    XDBG_RETURN_VAL_IF_FAIL (dst!=NULL, TRUE);
    XDBG_TRACE (MEXAH, "dst(%p, %d) %d,%d,%d,%d\n", dst, dst_pitch, x,y,w,h);

    gOpDFS.pSrc = pSrc;
    gOpDFS.x = x;
    gOpDFS.y = y;
    gOpDFS.w = w;
    gOpDFS.h = h;
    gOpDFS.dst = dst;
    gOpDFS.dst_pitch = dst_pitch;

    if(_g2dIsSupport(pSrc, FALSE))
    {
        gOpDFS.pOpSrc = _g2dPrepareAccess (pSrc,
                                                                    EXA_PREPARE_DEST,
                                                                    TBM_DEVICE_2D);
        gOpDFS.imgDst = g2d_image_create_data (gOpDFS.pOpSrc->buf[0].imgG2d->color_mode,
                                                                    w, h, (void*)dst, dst_pitch);
        gOpDFS.bDo = DO_DRAW_HW;
    }
    else
    {
        gOpDFS.pOpSrc = _g2dPrepareAccess (pSrc,
                                                                    EXA_PREPARE_DEST,
                                                                    TBM_DEVICE_CPU);
        gOpDFS.imgDst = NULL;
        gOpDFS.bDo = DO_DRAW_SW;
    }

    XDBG_TRACE (MEXAH, "%s\n", DO(gOpDFS));
    if (gOpDFS.pOpSrc->isSame)
    {
        ExaBox box;

        box.box.x1 = 0;
        box.box.y1 = 0;
        box.box.x2 = w;
        box.box.y2 = h;
        box.state = rgnIN;
        box.pSrc = &gOpDFS.pOpSrc->buf[0];
        _g2dDoDownladFromScreen (&box, NULL);
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
            box = _g2dBoxAdd (&gOpDFS.opBox,
                             &gOpDFS.pOpSrc->buf[i].pos,
                             &b);
            if (box)
            {
                box->pSrc = &gOpDFS.pOpSrc->buf[i];
            }
        }
        _g2dBoxMove (&gOpDFS.opBox, -x, -y);

        /* Call solid function */
        _g2dDoDraw (&gOpDFS.opBox,
                   _g2dDoDownladFromScreen, NULL);

        /*Remove box list*/
        _g2dBoxRemoveAll (&gOpDFS.opBox);
    }

    if(gOpDFS.bDo == DO_DRAW_HW)
        g2d_exec();

    _g2dFinishAccess (pSrc, EXA_PREPARE_SRC);
    if(gOpDFS.imgDst)
    {
        g2d_image_free(gOpDFS.imgDst);
    }
    return TRUE;
}

Bool secExaG2dInit (ScreenPtr pScreen, ExaDriverPtr pExaDriver)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = SECPTR (pScrn);

    if(!g2d_init (pSec->drm_fd))
    {
        XDBG_WARNING (MEXA, "[EXAG2D] fail to g2d_init(%d)\n", pSec->drm_fd);
    }

    pExaDriver->PrepareSolid = SECExaG2dPrepareSolid;
    pExaDriver->Solid = SECExaG2dSolid;
    pExaDriver->DoneSolid = SECExaG2dDoneSolid;

    pExaDriver->PrepareCopy = SECExaG2dPrepareCopy;
    pExaDriver->Copy = SECExaG2dCopy;
    pExaDriver->DoneCopy = SECExaG2dDoneCopy;

    pExaDriver->CheckComposite = SECExaG2dCheckComposite;
    pExaDriver->PrepareComposite = SECExaG2dPrepareComposite;
    pExaDriver->Composite = SECExaG2dComposite;
    pExaDriver->DoneComposite = SECExaG2dDoneComposite;

    pExaDriver->UploadToScreen = SECExaG2dUploadToScreen;
    pExaDriver->DownloadFromScreen = SECExaG2dDownloadFromScreen;

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "Succeed to Initialize G2D EXA\n");

    return TRUE;
}

void secExaG2dDeinit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

    xf86DrvMsg (pScrn->scrnIndex, X_INFO
                , "Succeed to finish SW EXA\n");
}
