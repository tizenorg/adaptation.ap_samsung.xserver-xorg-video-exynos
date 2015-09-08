/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: Boram Park <boram1288.park@samsung.com>

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

#include <sys/ioctl.h>

#include "sec.h"
#include "sec_util.h"
#include "sec_video_types.h"
#include "sec_video_fourcc.h"
#include "sec_drm_ipp.h"
#include "sec_converter.h"

#include <drm_fourcc.h>

//#define INCREASE_NUM 1
#define DEQUEUE_FORCE 1

#if INCREASE_NUM
#define CVT_BUF_MAX    6
#endif

typedef struct _SECCvtFuncData
{
    CvtFunc  func;
    void    *data;
    struct xorg_list   link;
} SECCvtFuncData;

typedef struct _SECCvtBuf
{
    SECCvtType    type;
    int           index;
    unsigned int  handles[EXYNOS_DRM_PLANAR_MAX];
    CARD32        begin;

    SECVideoBuf  *vbuf;

    struct xorg_list   link;
} SECCvtBuf;

struct _SECCvt
{
    CARD32        stamp;

    int  prop_id;

    ScrnInfoPtr   pScrn;
    SECCvtOp      op;
    SECCvtProp    props[CVT_TYPE_MAX];

    struct xorg_list   func_datas;
    struct xorg_list   src_bufs;
    struct xorg_list   dst_bufs;

#if INCREASE_NUM
    int           src_index;
    int           dst_index;
#endif

    Bool          started;
    Bool          first_event;

    struct xorg_list   link;
};

static unsigned int formats[] =
{
    FOURCC_RGB565,
    FOURCC_SR16,
    FOURCC_RGB32,
    FOURCC_SR32,
    FOURCC_YV12,
    FOURCC_I420,
    FOURCC_S420,
    FOURCC_ST12,
    FOURCC_SN12,
    FOURCC_NV12,
    FOURCC_SN21,
    FOURCC_NV21,
    FOURCC_YUY2,
    FOURCC_SUYV,
    FOURCC_UYVY,
    FOURCC_SYVY,
    FOURCC_ITLV,
};

static struct xorg_list cvt_list;

static void
_initList (void)
{
    static Bool inited = FALSE;

    if (inited)
        return;

    xorg_list_init (&cvt_list);

    inited = TRUE;
}

static SECCvt*
_findCvt (CARD32 stamp)
{
    SECCvt *cur = NULL, *next = NULL;

    _initList ();

    if (cvt_list.next != NULL)
    {
        xorg_list_for_each_entry_safe (cur, next, &cvt_list, link)
        {
            if (cur->stamp == stamp)
                return cur;
        }
    }

    return NULL;
}

static enum drm_exynos_ipp_cmd
_drmCommand (SECCvtOp op)
{
    switch (op)
    {
    case CVT_OP_OUTPUT:
        return IPP_CMD_OUTPUT;
    default:
        return IPP_CMD_M2M;
    }
}

static enum drm_exynos_degree
_drmDegree (int degree)
{
    switch (degree % 360)
    {
    case 90:
        return EXYNOS_DRM_DEGREE_90;
    case 180:
        return EXYNOS_DRM_DEGREE_180;
    case 270:
        return EXYNOS_DRM_DEGREE_270;
    default:
        return EXYNOS_DRM_DEGREE_0;
    }
}

static void
_FillConfig (SECCvtType type, SECCvtProp *prop, struct drm_exynos_ipp_config *config)
{
    config->ops_id = (type == CVT_TYPE_SRC) ? EXYNOS_DRM_OPS_SRC : EXYNOS_DRM_OPS_DST;

    if (prop->hflip)
        config->flip |= EXYNOS_DRM_FLIP_HORIZONTAL;
    if (prop->vflip)
        config->flip |= EXYNOS_DRM_FLIP_VERTICAL;

    config->degree = _drmDegree (prop->degree);
    config->fmt = secUtilGetDrmFormat (prop->id);
    config->sz.hsize = (__u32)prop->width;
    config->sz.vsize = (__u32)prop->height;
    config->pos.x = (__u32)prop->crop.x;
    config->pos.y = (__u32)prop->crop.y;
    config->pos.w = (__u32)prop->crop.width;
    config->pos.h = (__u32)prop->crop.height;
}

static void
_fillProperty (SECCvt *cvt, SECCvtType type, SECVideoBuf *vbuf, SECCvtProp *prop)
{
    prop->id = vbuf->id;
    prop->width = vbuf->width;
    prop->height = vbuf->height;
    prop->crop = vbuf->crop;

    prop->degree = cvt->props[type].degree;
    prop->vflip = cvt->props[type].vflip;
    prop->hflip = cvt->props[type].hflip;
    prop->secure = cvt->props[type].secure;
    prop->csc_range = cvt->props[type].csc_range;
}

static Bool
_SetVbufConverting (SECVideoBuf *vbuf, SECCvt *cvt, Bool converting)
{
    if (!converting)
    {
        ConvertInfo *cur = NULL, *next = NULL;

        xorg_list_for_each_entry_safe (cur, next, &vbuf->convert_info, link)
        {
            if (cur->cvt == (void*)cvt)
            {
                xorg_list_del (&cur->link);
                free (cur);
                return TRUE;
            }
        }

        XDBG_ERROR (MCVT, "failed: %ld not found in %ld.\n", cvt->stamp, vbuf->stamp);
        return FALSE;
    }
    else
    {
        ConvertInfo *info = NULL, *next = NULL;

        xorg_list_for_each_entry_safe (info, next, &vbuf->convert_info, link)
        {
            if (info->cvt == (void*)cvt)
            {
                XDBG_ERROR (MCVT, "failed: %ld already converting %ld.\n", cvt->stamp, vbuf->stamp);
                return FALSE;
            }
        }

        info = calloc (1, sizeof (ConvertInfo));
        XDBG_RETURN_VAL_IF_FAIL (info != NULL, FALSE);

        info->cvt = (void*)cvt;

        xorg_list_add (&info->link, &vbuf->convert_info);

        return TRUE;
    }
}

#if 0
static void
_printBufIndices (SECCvt *cvt, SECCvtType type, char *str)
{
    struct xorg_list *bufs;
    SECCvtBuf *cur, *next;
    char nums[128];

    bufs = (type == CVT_TYPE_SRC) ? &cvt->src_bufs : &cvt->dst_bufs;

    snprintf (nums, 128, "bufs:");

    list_rev_for_each_entry_safe (cur, next, bufs, link)
    {
        snprintf (nums, 128, "%s %d", nums, cur->index);
    }

    ErrorF ("%s: cvt(%p) %s(%s). \n", str, cvt,
               (type == CVT_TYPE_SRC)?"SRC":"DST", nums);
}
#endif

static int
_secCvtGetEmptyIndex (SECCvt *cvt, SECCvtType type)
{
#if INCREASE_NUM
    int ret;

    if(type == CVT_TYPE_SRC)
    {
        ret = cvt->src_index++;
        if (cvt->src_index >= CVT_BUF_MAX)
            cvt->src_index = 0;
    }
    else
    {
        ret = cvt->dst_index++;
        if (cvt->dst_index >= CVT_BUF_MAX)
            cvt->dst_index = 0;
    }

    return ret;
#else
    struct xorg_list *bufs;
    SECCvtBuf *cur = NULL, *next = NULL;
    int ret = 0;

    bufs = (type == CVT_TYPE_SRC) ? &cvt->src_bufs : &cvt->dst_bufs;

    while (1)
    {
        Bool found = FALSE;

        xorg_list_for_each_entry_safe (cur, next, bufs, link)
        {
            if (ret == cur->index)
            {
                found = TRUE;
                break;
            }
        }

        if (!found)
            break;

        ret++;
    }

    return ret;
#endif
}

static SECCvtBuf*
_secCvtFindBuf (SECCvt *cvt, SECCvtType type, int index)
{
    struct xorg_list *bufs;
    SECCvtBuf *cur = NULL, *next = NULL;

    bufs = (type == CVT_TYPE_SRC) ? &cvt->src_bufs : &cvt->dst_bufs;

    xorg_list_for_each_entry_safe (cur, next, bufs, link)
    {
        if (index == cur->index)
            return cur;
    }

    XDBG_ERROR (MCVT, "cvt(%p), type(%d), index(%d) not found.\n", cvt, type, index);

    return NULL;
}

static Bool
_secCvtQueue (SECCvt *cvt, SECCvtBuf *cbuf)
{
    struct drm_exynos_ipp_queue_buf buf = {0,};
    struct xorg_list *bufs;
    int i;
    int index = _secCvtGetEmptyIndex (cvt, cbuf->type);

    buf.prop_id = cvt->prop_id;
    buf.ops_id = (cbuf->type == CVT_TYPE_SRC) ? EXYNOS_DRM_OPS_SRC : EXYNOS_DRM_OPS_DST;
    buf.buf_type = IPP_BUF_ENQUEUE;
    buf.buf_id = cbuf->index = index;
    buf.user_data = (__u64)cvt->stamp;

    for (i = 0; i < EXYNOS_DRM_PLANAR_MAX; i++)
        buf.handle[i] = (__u32)cbuf->handles[i];

    if (!secDrmIppQueueBuf (cvt->pScrn, &buf))
        return FALSE;

    bufs = (cbuf->type == CVT_TYPE_SRC) ? &cvt->src_bufs : &cvt->dst_bufs;
    xorg_list_add (&cbuf->link, bufs);

    _SetVbufConverting (cbuf->vbuf, cvt, TRUE);

#if 0
    if (cbuf->type == CVT_TYPE_SRC)
        _printBufIndices (cvt, CVT_TYPE_SRC, "in");
#endif

    XDBG_DEBUG (MCVT, "cvt(%p), cbuf(%p), type(%d), index(%d) vbuf(%p) converting(%d)\n",
                cvt, cbuf, cbuf->type, index, cbuf->vbuf, VBUF_IS_CONVERTING (cbuf->vbuf));

    return TRUE;
}

static void
_secCvtDequeue (SECCvt *cvt, SECCvtBuf *cbuf)
{
    struct drm_exynos_ipp_queue_buf buf = {0,};
    int i;

    if (!_secCvtFindBuf (cvt, cbuf->type, cbuf->index))
    {
        XDBG_WARNING (MCVT, "cvt(%p) type(%d), index(%d) already dequeued!\n",
                      cvt, cbuf->type, cbuf->index);
        return;
    }

    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (cbuf->vbuf));

    buf.prop_id = cvt->prop_id;
    buf.ops_id = (cbuf->type == CVT_TYPE_SRC) ? EXYNOS_DRM_OPS_SRC : EXYNOS_DRM_OPS_DST;
    buf.buf_type = IPP_BUF_DEQUEUE;
    buf.buf_id = cbuf->index;
    buf.user_data = (__u64)cvt->stamp;

    for (i = 0; i < EXYNOS_DRM_PLANAR_MAX; i++)
        buf.handle[i] = (__u32)cbuf->handles[i];

    if (!secDrmIppQueueBuf (cvt->pScrn, &buf))
        return;
}

static void
_secCvtDequeued (SECCvt *cvt, SECCvtType type, int index)
{
    SECCvtBuf *cbuf = _secCvtFindBuf (cvt, type, index);

    if (!cbuf)
    {
        XDBG_WARNING (MCVT, "cvt(%p) type(%d), index(%d) already dequeued!\n",
                      cvt, type, index);
        return;
    }

    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (cbuf->vbuf));

    _SetVbufConverting (cbuf->vbuf, cvt, FALSE);

    XDBG_DEBUG (MCVT, "cvt(%p) type(%d) index(%d) vbuf(%p) converting(%d)\n",
                cvt, type, index, cbuf->vbuf, VBUF_IS_CONVERTING (cbuf->vbuf));

    xorg_list_del (&cbuf->link);

#if 0
    if (cbuf->type == CVT_TYPE_SRC)
        _printBufIndices (cvt, CVT_TYPE_SRC, "out");
#endif

    secUtilVideoBufferUnref (cbuf->vbuf);
    free (cbuf);
}

static void
_secCvtDequeueAll (SECCvt *cvt)
{
    SECCvtBuf *cur = NULL, *next = NULL;

    xorg_list_for_each_entry_safe (cur, next, &cvt->src_bufs, link)
    {
        _secCvtDequeue (cvt, cur);
    }

    xorg_list_for_each_entry_safe (cur, next, &cvt->dst_bufs, link)
    {
        _secCvtDequeue (cvt, cur);
    }
}

static void
_secCvtDequeuedAll (SECCvt *cvt)
{
    SECCvtBuf *cur = NULL, *next = NULL;

    xorg_list_for_each_entry_safe (cur, next, &cvt->src_bufs, link)
    {
        _secCvtDequeued (cvt, EXYNOS_DRM_OPS_SRC, cur->index);
    }

    xorg_list_for_each_entry_safe (cur, next, &cvt->dst_bufs, link)
    {
        _secCvtDequeued (cvt, EXYNOS_DRM_OPS_DST, cur->index);
    }
}

static void
_secCvtStop (SECCvt *cvt)
{
    struct drm_exynos_ipp_cmd_ctrl ctrl = {0,};

    XDBG_RETURN_IF_FAIL (cvt != NULL);

    if (!cvt->started)
        return;

    _secCvtDequeueAll (cvt);

    ctrl.prop_id = cvt->prop_id;
    ctrl.ctrl = IPP_CTRL_STOP;

    secDrmIppCmdCtrl (cvt->pScrn, &ctrl);

    _secCvtDequeuedAll (cvt);

    XDBG_TRACE (MCVT, "cvt(%p)\n", cvt);

    cvt->prop_id = -1;

    memset (cvt->props, 0, sizeof (SECCvtProp) * CVT_TYPE_MAX);

#if INCREASE_NUM
    cvt->src_index = 0;
    cvt->dst_index = 0;
#endif
    cvt->started = FALSE;

    return;
}

Bool
secCvtSupportFormat (SECCvtOp op, int id)
{
    unsigned int *drmfmts;
    int i, size, num = 0;
    unsigned int drmfmt = secUtilGetDrmFormat (id);

    XDBG_RETURN_VAL_IF_FAIL (op >= 0 && op < CVT_OP_MAX, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (id > 0, FALSE);

    size = sizeof (formats) / sizeof (unsigned int);

    for (i = 0; i < size; i++)
        if (formats[i] == id)
            break;

    if (i == size)
    {
        XDBG_ERROR (MCVT, "converter(op:%d) not support : '%c%c%c%c'.\n",
                    op, FOURCC_STR (id));
        return FALSE;
    }

    drmfmts = secDrmIppGetFormatList (&num);
    if (!drmfmts)
    {
        XDBG_ERROR (MCVT, "no drm format list.\n");
        return FALSE;
    }

    for (i = 0; i < num; i++)
        if (drmfmts[i] == drmfmt)
        {
            free (drmfmts);
            return TRUE;
        }

    XDBG_ERROR (MCVT, "drm ipp not support : '%c%c%c%c'.\n", FOURCC_STR (id));

    free (drmfmts);

    return FALSE;
}

Bool
secCvtEnsureSize (SECCvtProp *src, SECCvtProp *dst)
{
    if (src)
    {
        int type = secUtilGetColorType (src->id);

        XDBG_RETURN_VAL_IF_FAIL (src->width >= 16, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (src->height >= 8, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (src->crop.width >= 16, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (src->crop.height >= 8, FALSE);

        if (src->width % 16)
        {
            if (!IS_ZEROCOPY (src->id))
            {
                int new_width = (src->width + 16) & (~0xF);
                XDBG_DEBUG (MCVT, "src's width : %d to %d.\n", src->width, new_width);
                src->width = new_width;
            }
        }

        if (type == TYPE_YUV420 && src->height % 2)
            XDBG_WARNING (MCVT, "src's height(%d) is not multiple of 2!!!\n", src->height);

        if (type == TYPE_YUV420 || type == TYPE_YUV422)
        {
            src->crop.x = src->crop.x & (~0x1);
            src->crop.width = src->crop.width & (~0x1);
        }

        if (type == TYPE_YUV420)
            src->crop.height = src->crop.height & (~0x1);

        if (src->crop.x + src->crop.width > src->width)
            src->crop.width = src->width - src->crop.x;
        if (src->crop.y + src->crop.height > src->height)
            src->crop.height = src->height - src->crop.y;
    }

    if (dst)
    {
        int type = secUtilGetColorType (dst->id);

        XDBG_RETURN_VAL_IF_FAIL (dst->width >= 16, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (dst->height >= 8, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (dst->crop.width >= 16, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (dst->crop.height >= 4, FALSE);

        if (dst->width % 16)
        {
            int new_width = (dst->width + 16) & (~0xF);
            XDBG_DEBUG (MCVT, "dst's width : %d to %d.\n", dst->width, new_width);
            dst->width = new_width;
        }

        dst->height = dst->height & (~0x1);

        if (type == TYPE_YUV420 && dst->height % 2)
            XDBG_WARNING (MCVT, "dst's height(%d) is not multiple of 2!!!\n", dst->height);

        if (type == TYPE_YUV420 || type == TYPE_YUV422)
        {
            dst->crop.x = dst->crop.x & (~0x1);
            dst->crop.width = dst->crop.width & (~0x1);
        }

        if (type == TYPE_YUV420)
            dst->crop.height = dst->crop.height & (~0x1);

        if (dst->crop.x + dst->crop.width > dst->width)
            dst->crop.width = dst->width - dst->crop.x;
        if (dst->crop.y + dst->crop.height > dst->height)
            dst->crop.height = dst->height - dst->crop.y;
    }

    return TRUE;
}

SECCvt*
secCvtCreate (ScrnInfoPtr pScrn, SECCvtOp op)
{
    SECCvt *cvt;
    CARD32 stamp = GetTimeInMillis ();

    _initList ();

    XDBG_RETURN_VAL_IF_FAIL (pScrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (op >= 0 && op < CVT_OP_MAX, NULL);

    while (_findCvt (stamp))
        stamp++;

    cvt = calloc (1, sizeof (SECCvt));
    XDBG_RETURN_VAL_IF_FAIL (cvt != NULL, NULL);

    cvt->stamp = stamp;

    cvt->pScrn = pScrn;
    cvt->op = op;
    cvt->prop_id = -1;

    xorg_list_init (&cvt->func_datas);
    xorg_list_init (&cvt->src_bufs);
    xorg_list_init (&cvt->dst_bufs);

    XDBG_TRACE (MCVT, "op(%d), cvt(%p) stamp(%ld)\n", op, cvt, stamp);

    xorg_list_add (&cvt->link, &cvt_list);

    return cvt;
}

void
secCvtDestroy (SECCvt *cvt)
{
    SECCvtFuncData *cur = NULL, *next = NULL;

    if (!cvt)
        return;

    _secCvtStop (cvt);

    xorg_list_del (&cvt->link);

    xorg_list_for_each_entry_safe (cur, next, &cvt->func_datas, link)
    {
        xorg_list_del (&cur->link);
        free (cur);
    }

    XDBG_TRACE (MCVT, "cvt(%p)\n", cvt);

    free (cvt);
}

SECCvtOp
secCvtGetOp (SECCvt *cvt)
{
    XDBG_RETURN_VAL_IF_FAIL (cvt != NULL, CVT_OP_M2M);

    return cvt->op;
}

Bool
secCvtSetProperpty (SECCvt *cvt, SECCvtProp *src, SECCvtProp *dst)
{
    if (cvt->started)
        return TRUE;

    struct drm_exynos_ipp_property property;

    XDBG_RETURN_VAL_IF_FAIL (cvt != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst != NULL, FALSE);

    if (!secCvtEnsureSize (src, dst))
        return FALSE;

    if (dst->crop.x + dst->crop.width > dst->width)
    {
        XDBG_ERROR (MCVT, "dst(%d+%d > %d). !\n", dst->crop.x, dst->crop.width, dst->width);
    }

    if (!secCvtSupportFormat (cvt->op, src->id))
    {
        XDBG_ERROR (MCVT, "cvt(%p) src '%c%c%c%c' not supported.\n",
                    cvt, FOURCC_STR (src->id));
        return FALSE;
    }

    if (!secCvtSupportFormat (cvt->op, dst->id))
    {
        XDBG_ERROR (MCVT, "cvt(%p) dst '%c%c%c%c' not supported.\n",
                    cvt, FOURCC_STR (dst->id));
        return FALSE;
    }

    memcpy (&cvt->props[CVT_TYPE_SRC], src, sizeof (SECCvtProp));
    memcpy (&cvt->props[CVT_TYPE_DST], dst, sizeof (SECCvtProp));

    CLEAR (property);
    _FillConfig (CVT_TYPE_SRC, &cvt->props[CVT_TYPE_SRC], &property.config[0]);
    _FillConfig (CVT_TYPE_DST, &cvt->props[CVT_TYPE_DST], &property.config[1]);
    property.cmd = _drmCommand (cvt->op);
#ifdef _F_WEARABLE_FEATURE_
    property.type = IPP_EVENT_DRIVEN;
#endif
    property.prop_id = cvt->prop_id;
    property.protect = dst->secure;
    property.range = dst->csc_range;

    XDBG_TRACE (MCVT, "cvt(%p) src('%c%c%c%c', '%c%c%c%c', %dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                cvt, FOURCC_STR(src->id), FOURCC_STR(secUtilGetDrmFormat(src->id)),
                src->width, src->height,
                src->crop.x, src->crop.y, src->crop.width, src->crop.height,
                src->degree, src->hflip, src->vflip,
                src->secure, src->csc_range);

    XDBG_TRACE (MCVT, "cvt(%p) dst('%c%c%c%c', '%c%c%c%c',%dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                cvt, FOURCC_STR(dst->id), FOURCC_STR(secUtilGetDrmFormat(dst->id)),
                dst->width, dst->height,
                dst->crop.x, dst->crop.y, dst->crop.width, dst->crop.height,
                dst->degree, dst->hflip, dst->vflip,
                dst->secure, dst->csc_range);

    cvt->prop_id = secDrmIppSetProperty (cvt->pScrn, &property);
    XDBG_RETURN_VAL_IF_FAIL (cvt->prop_id >= 0, FALSE);

    return TRUE;
}

void
secCvtGetProperpty (SECCvt *cvt, SECCvtProp *src, SECCvtProp *dst)
{
    XDBG_RETURN_IF_FAIL (cvt != NULL);

    if (src)
        *src = cvt->props[CVT_TYPE_SRC];

    if (dst)
        *dst = cvt->props[CVT_TYPE_DST];
}

Bool
secCvtConvert (SECCvt *cvt, SECVideoBuf *src, SECVideoBuf *dst)
{
    SECCvtBuf *src_cbuf = NULL, *dst_cbuf = NULL;
    SECCvtProp src_prop, dst_prop;

    XDBG_RETURN_VAL_IF_FAIL (cvt != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (cvt->op == CVT_OP_M2M, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (src), FALSE);
    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (dst), FALSE);

    _fillProperty (cvt, CVT_TYPE_SRC, src, &src_prop);
    _fillProperty (cvt, CVT_TYPE_DST, dst, &dst_prop);

    if (memcmp (&cvt->props[CVT_TYPE_SRC], &src_prop, sizeof (SECCvtProp)) ||
        memcmp (&cvt->props[CVT_TYPE_DST], &dst_prop, sizeof (SECCvtProp)))
    {
        XDBG_ERROR (MCVT, "cvt(%p) prop changed!\n", cvt);
        XDBG_TRACE (MCVT, "cvt(%p) src_old('%c%c%c%c', %dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                    cvt, FOURCC_STR(cvt->props[CVT_TYPE_SRC].id),
                    cvt->props[CVT_TYPE_SRC].width, cvt->props[CVT_TYPE_SRC].height,
                    cvt->props[CVT_TYPE_SRC].crop.x, cvt->props[CVT_TYPE_SRC].crop.y,
                    cvt->props[CVT_TYPE_SRC].crop.width, cvt->props[CVT_TYPE_SRC].crop.height,
                    cvt->props[CVT_TYPE_SRC].degree,
                    cvt->props[CVT_TYPE_SRC].hflip, cvt->props[CVT_TYPE_SRC].vflip,
                    cvt->props[CVT_TYPE_SRC].secure, cvt->props[CVT_TYPE_SRC].csc_range);
        XDBG_TRACE (MCVT, "cvt(%p) src_new('%c%c%c%c', %dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                    cvt, FOURCC_STR(src_prop.id), src_prop.width, src_prop.height,
                    src_prop.crop.x, src_prop.crop.y, src_prop.crop.width, src_prop.crop.height,
                    src_prop.degree, src_prop.hflip, src_prop.vflip,
                    src_prop.secure, src_prop.csc_range);
        XDBG_TRACE (MCVT, "cvt(%p) dst_old('%c%c%c%c', %dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                    cvt, FOURCC_STR(cvt->props[CVT_TYPE_DST].id),
                    cvt->props[CVT_TYPE_DST].width, cvt->props[CVT_TYPE_DST].height,
                    cvt->props[CVT_TYPE_DST].crop.x, cvt->props[CVT_TYPE_DST].crop.y,
                    cvt->props[CVT_TYPE_DST].crop.width, cvt->props[CVT_TYPE_DST].crop.height,
                    cvt->props[CVT_TYPE_DST].degree,
                    cvt->props[CVT_TYPE_DST].hflip, cvt->props[CVT_TYPE_DST].vflip,
                    cvt->props[CVT_TYPE_DST].secure, cvt->props[CVT_TYPE_DST].csc_range);
        XDBG_TRACE (MCVT, "cvt(%p) dst_new('%c%c%c%c', %dx%d, %d,%d %dx%d, %d, %d&%d, %d, %d)\n",
                    cvt, FOURCC_STR(dst_prop.id), dst_prop.width, dst_prop.height,
                    dst_prop.crop.x, dst_prop.crop.y, dst_prop.crop.width, dst_prop.crop.height,
                    dst_prop.degree, dst_prop.hflip, dst_prop.vflip,
                    dst_prop.secure, dst_prop.csc_range);
        return FALSE;
    }

    XDBG_GOTO_IF_FAIL (cvt->prop_id >= 0, fail_to_convert);
    XDBG_GOTO_IF_FAIL (src->handles[0] > 0, fail_to_convert);
    XDBG_GOTO_IF_FAIL (dst->handles[0] > 0, fail_to_convert);

    src_cbuf = calloc (1, sizeof (SECCvtBuf));
    XDBG_GOTO_IF_FAIL (src_cbuf != NULL, fail_to_convert);
    dst_cbuf = calloc (1, sizeof (SECCvtBuf));
    XDBG_GOTO_IF_FAIL (dst_cbuf != NULL, fail_to_convert);

    src_cbuf->type = CVT_TYPE_SRC;
    src_cbuf->vbuf = secUtilVideoBufferRef (src);
    XDBG_GOTO_IF_FAIL (src_cbuf->vbuf != NULL, fail_to_convert);
    memcpy (src_cbuf->handles, src->handles, sizeof (unsigned int) * EXYNOS_DRM_PLANAR_MAX);

    if (!_secCvtQueue (cvt, src_cbuf))
    {
        secUtilVideoBufferUnref (src_cbuf->vbuf);
        goto fail_to_convert;
    }

    XDBG_DEBUG (MCVT, "cvt(%p) srcbuf(%p) converting(%d)\n",
                cvt, src, VBUF_IS_CONVERTING (src));

    dst_cbuf->type = CVT_TYPE_DST;
    dst_cbuf->vbuf = secUtilVideoBufferRef (dst);
    memcpy (dst_cbuf->handles, dst->handles, sizeof (unsigned int) * EXYNOS_DRM_PLANAR_MAX);

    if (!_secCvtQueue (cvt, dst_cbuf))
    {
        secUtilVideoBufferUnref (dst_cbuf->vbuf);
        goto fail_to_convert;
    }

    XDBG_DEBUG (MCVT, "cvt(%p) dstbuf(%p) converting(%d)\n",
                cvt, dst, VBUF_IS_CONVERTING (dst));

    XDBG_DEBUG (MVBUF, "==> ipp (%ld,%d,%d : %ld,%d,%d) \n",
                src->stamp, VBUF_IS_CONVERTING (src), src->showing,
                dst->stamp, VBUF_IS_CONVERTING (dst), dst->showing);

    if (!cvt->started)
    {
        struct drm_exynos_ipp_cmd_ctrl ctrl = {0,};

        ctrl.prop_id = cvt->prop_id;
        ctrl.ctrl = IPP_CTRL_PLAY;

        if (!secDrmIppCmdCtrl (cvt->pScrn, &ctrl))
            goto fail_to_convert;

        XDBG_TRACE (MCVT, "cvt(%p) start.\n", cvt);

        cvt->started = TRUE;
    }

    dst->dirty = TRUE;

    SECPtr pSec = SECPTR (cvt->pScrn);
    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_CVT)
        src_cbuf->begin = GetTimeInMillis ();

    return TRUE;

fail_to_convert:

    if (src_cbuf)
        free (src_cbuf);
    if (dst_cbuf)
        free (dst_cbuf);

    _secCvtStop (cvt);

    return FALSE;
}

Bool
secCvtAddCallback (SECCvt *cvt, CvtFunc func, void *data)
{
    SECCvtFuncData *func_data;

    XDBG_RETURN_VAL_IF_FAIL (cvt != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (func != NULL, FALSE);

    func_data = calloc (1, sizeof (SECCvtFuncData));
    XDBG_RETURN_VAL_IF_FAIL (func_data != NULL, FALSE);

    xorg_list_add (&func_data->link, &cvt->func_datas);

    func_data->func = func;
    func_data->data = data;

    return TRUE;
}

void
secCvtRemoveCallback (SECCvt *cvt, CvtFunc func, void *data)
{
    SECCvtFuncData *cur = NULL, *next = NULL;

    XDBG_RETURN_IF_FAIL (cvt != NULL);
    XDBG_RETURN_IF_FAIL (func != NULL);

    xorg_list_for_each_entry_safe (cur, next, &cvt->func_datas, link)
    {
        if (cur->func == func && cur->data == data)
        {
            xorg_list_del (&cur->link);
            free (cur);
        }
    }
}

void
secCvtHandleIppEvent (int fd, unsigned int *buf_idx, void *data, Bool error)
{
    CARD32 stamp = (CARD32)data;
    SECCvt *cvt;
    SECCvtBuf *src_cbuf, *dst_cbuf;
    SECVideoBuf *src_vbuf, *dst_vbuf;
    SECCvtFuncData *curr = NULL, *next = NULL;

    XDBG_RETURN_IF_FAIL (buf_idx != NULL);

    cvt = _findCvt (stamp);
    if (!cvt)
    {
        XDBG_DEBUG (MCVT, "invalid cvt's stamp (%ld).\n", stamp);
        return;
    }

    XDBG_DEBUG (MCVT, "cvt(%p) index(%d, %d)\n",
                cvt, buf_idx[EXYNOS_DRM_OPS_SRC], buf_idx[EXYNOS_DRM_OPS_DST]);

#if 0
    char temp[64];
    snprintf (temp, 64, "%d,%d", buf_idx[EXYNOS_DRM_OPS_SRC], buf_idx[EXYNOS_DRM_OPS_DST]);
    _printBufIndices (cvt, CVT_TYPE_SRC, temp);
#endif

#if DEQUEUE_FORCE
    SECCvtBuf *cur = NULL, *prev = NULL;

    list_rev_for_each_entry_safe (cur, prev, &cvt->src_bufs, link)
    {
        if (buf_idx[EXYNOS_DRM_OPS_SRC] != cur->index)
        {
            unsigned int indice[EXYNOS_DRM_OPS_MAX] = {cur->index, cur->index};

            XDBG_WARNING (MCVT, "cvt(%p) event(%d,%d) has been skipped!! \n",
                          cvt, cur->index, cur->index);

            /* To make sure all events are received, _secCvtDequeued should be called
             * for every event. If a event has been skipped, to call _secCvtDequeued
             * forcely, we call secCvtHandleIppEvent recursivly.
             */
            secCvtHandleIppEvent (0, indice, (void*)cvt->stamp, TRUE);
        }
        else
            break;
    }
#endif

    src_cbuf = _secCvtFindBuf (cvt, EXYNOS_DRM_OPS_SRC, buf_idx[EXYNOS_DRM_OPS_SRC]);
    XDBG_RETURN_IF_FAIL (src_cbuf != NULL);

    dst_cbuf = _secCvtFindBuf (cvt, EXYNOS_DRM_OPS_DST, buf_idx[EXYNOS_DRM_OPS_DST]);
    XDBG_RETURN_IF_FAIL (dst_cbuf != NULL);

    SECPtr pSec = SECPTR (cvt->pScrn);
    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_CVT)
    {
        CARD32 cur, sub;
        cur = GetTimeInMillis ();
        sub = cur - src_cbuf->begin;
        ErrorF ("cvt(%p)   ipp interval  : %6ld ms\n", cvt, sub);
    }

    src_vbuf = src_cbuf->vbuf;
    dst_vbuf = dst_cbuf->vbuf;

    XDBG_DEBUG (MVBUF, "<== ipp (%ld,%d,%d : %ld,%d,%d) \n",
                src_vbuf->stamp, VBUF_IS_CONVERTING (src_vbuf), src_vbuf->showing,
                dst_vbuf->stamp, VBUF_IS_CONVERTING (dst_vbuf), dst_vbuf->showing);

    if (!cvt->first_event)
    {
        XDBG_DEBUG (MCVT, "cvt(%p) got a IPP event. \n", cvt);
        cvt->first_event = TRUE;
    }

    xorg_list_for_each_entry_safe (curr, next, &cvt->func_datas, link)
    {
        if (curr->func)
            curr->func (cvt, src_vbuf, dst_vbuf, curr->data, error);
    }

    _secCvtDequeued (cvt, EXYNOS_DRM_OPS_SRC, buf_idx[EXYNOS_DRM_OPS_SRC]);
    _secCvtDequeued (cvt, EXYNOS_DRM_OPS_DST, buf_idx[EXYNOS_DRM_OPS_DST]);
}
