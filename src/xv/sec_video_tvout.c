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

#include "sec.h"
#include "sec_display.h"
#include "sec_crtc.h"
#include "sec_output.h"
#include "sec_accel.h"
#include "sec_util.h"
#include "sec_converter.h"
#include "sec_video_tvout.h"
#include "sec_video_virtual.h"
#include "sec_video_fourcc.h"
#include "sec_drm_ipp.h"
#include "sec_layer.h"
#include "sec_prop.h"

#include <sys/ioctl.h>
#include <exynos_drm.h>
#include <drm_fourcc.h>

#define TVBUF_NUM  3

/* HW restriction (VP) */
#define MIN_WIDTH  32
#define MIN_HEIGHT 4
#define MAX_WIDTH  1920
#define MAX_HEIGHT 1080
#define MIN_SCALE  0.25
#define MAX_SCALE  16.0

/* private structure */
struct _SECVideoTv
{
    ScrnInfoPtr pScrn;

    SECLayer *layer;
    SECLayerPos lpos;

    /* used if id is not supported in case of lpos == LAYER_LOWER1. */
    SECCvt      *cvt;
    SECVideoBuf **outbuf;
    int          outbuf_index;
    int          outbuf_num;

    int          tv_width;
    int          tv_height;

    xRectangle   tv_rect;

    unsigned int convert_id;
};

static Bool
_secVieoTvCalSize (SECVideoTv* tv, int src_w, int src_h, int dst_w, int dst_h)
{
    float r;

    if (src_w < MIN_WIDTH || src_h < MIN_HEIGHT)
    {
        XDBG_WARNING (MTVO, "size(%dx%d) must be more than (%dx%d).\n",
                      src_w, src_h, MIN_WIDTH, MAX_WIDTH);
    }

    r = (float)dst_w / src_w;
    if (r < MIN_SCALE || r > MAX_SCALE)
    {
        XDBG_WARNING (MTVO, "ratio_w(%f) is out of range(%f~%f).\n",
                      r, MIN_SCALE, MAX_SCALE);
    }

    r = (float)dst_h / src_h;
    if (r < MIN_SCALE || r > MAX_SCALE)
    {
        XDBG_WARNING (MTVO, "ratio_h(%d) is out of range(%f~%f).\n",
                      r, MIN_SCALE, MAX_SCALE);
    }

    return TRUE;
}

static SECVideoBuf*
_secVideoTvGetOutBuffer (SECVideoTv* tv, int width, int height, Bool secure)
{
    int i = tv->outbuf_index, j;

    if (!tv->outbuf)
    {
        tv->outbuf = (SECVideoBuf**)calloc (tv->outbuf_num, sizeof (SECVideoBuf*));
        XDBG_RETURN_VAL_IF_FAIL (tv->outbuf != NULL, NULL);
    }

    i++;
    if (i >= tv->outbuf_num)
        i = 0;

    for (j = 0; j < tv->outbuf_num; j++)
    {
        if (tv->outbuf[i])
        {
            XDBG_DEBUG (MTVO, "outbuf(%p) converting(%d) showing(%d)\n",
                        tv->outbuf[i], VBUF_IS_CONVERTING (tv->outbuf[i]), tv->outbuf[i]->showing);

            if (tv->outbuf[i] && !VBUF_IS_CONVERTING (tv->outbuf[i]) && !tv->outbuf[i]->showing)
            {
                tv->outbuf_index = i;
                return tv->outbuf[i];
            }
        }
        else
        {
            SECPtr pSec = SECPTR (tv->pScrn);

            tv->outbuf[i] = secUtilAllocVideoBuffer (tv->pScrn, tv->convert_id, width, height,
                                                     (pSec->scanout)?TRUE:FALSE, TRUE, secure);
            XDBG_RETURN_VAL_IF_FAIL (tv->outbuf[i] != NULL, NULL);

            XDBG_DEBUG (MTVO, "outbuf(%p, %c%c%c%c)\n", tv->outbuf[i], FOURCC_STR (tv->convert_id));

            tv->outbuf_index = i;
            return tv->outbuf[i];
        }

        i++;
        if (i >= tv->outbuf_num)
            i = 0;
    }

#if 0
    char buffers[1024];
    CLEAR (buffers);
    for (j = 0; j < tv->outbuf_num; j++)
    {
        if (tv->outbuf[j])
            snprintf (buffers, 1024, "%s %d(%d,%d) ", buffers, tv->outbuf[j]->keys[0],
            VBUF_IS_CONVERTING (tv->outbuf[j]), tv->outbuf[j]->showing);
    }

    XDBG_ERROR (MTVO, "now all outbufs in use! %s\n", buffers);
#else
    XDBG_ERROR (MTVO, "now all outbufs in use!\n");
#endif

    return NULL;
}

static Bool
_secVideoTvLayerEnsure (SECVideoTv* tv)
{
    SECLayer *layer;

    if (tv->layer)
        return TRUE;

    XDBG_RETURN_VAL_IF_FAIL (tv->lpos != LAYER_NONE, FALSE);

    layer = secLayerCreate (tv->pScrn, LAYER_OUTPUT_EXT, tv->lpos);
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);

    tv->layer = layer;

    return TRUE;
}

static void
_secVideoTvLayerDestroy (SECVideoTv* tv)
{
    if (tv->layer)
    {
        secLayerUnref (tv->layer);
        tv->layer = NULL;
    }
}

static int
_secVideoTvPutImageInternal (SECVideoTv *tv, SECVideoBuf *vbuf, xRectangle *rect)
{
    int ret = 0;

    XDBG_DEBUG (MTVO, "rect (%d,%d %dx%d) \n",
                rect->x, rect->y, rect->width, rect->height);

    secLayerSetRect (tv->layer, &vbuf->crop, rect);

    if (tv->lpos == LAYER_LOWER1)
        if (!_secVieoTvCalSize (tv, vbuf->width, vbuf->height,
                                rect->width, rect->height))
            return 0;

    ret = secLayerSetBuffer (tv->layer, vbuf);

    if (ret == 0)
        return 0;

    secLayerShow (tv->layer);

    return ret;
}

static void
_secVideoTvCvtCallback (SECCvt *cvt,
                        SECVideoBuf *src,
                        SECVideoBuf *dst,
                        void *cvt_data,
                        Bool error)
{
    SECVideoTv *tv = (SECVideoTv*)cvt_data;
    int i;

    XDBG_RETURN_IF_FAIL (tv != NULL);
    XDBG_RETURN_IF_FAIL (cvt != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (dst));

    XDBG_DEBUG (MTVO, "++++++++++++++++++++++++ \n");

    for (i = 0; i < tv->outbuf_num; i++)
        if (tv->outbuf[i] == dst)
            break;
    XDBG_RETURN_IF_FAIL (i < tv->outbuf_num);

    _secVideoTvPutImageInternal (tv, dst, &tv->tv_rect);

    XDBG_DEBUG (MTVO, "++++++++++++++++++++++++.. \n");
}

SECVideoTv*
secVideoTvConnect (ScrnInfoPtr pScrn, unsigned int id, SECLayerPos lpos)
{
    SECVideoTv* tv = NULL;
    SECModePtr pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (pScrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (lpos >= LAYER_LOWER1 && lpos <= LAYER_UPPER, NULL);
    XDBG_RETURN_VAL_IF_FAIL (id > 0, NULL);

    tv = calloc (sizeof (SECVideoTv), 1);
    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, NULL);

    pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;

    if (lpos == LAYER_LOWER1 && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
    {
        /* In case of video-only(virtual), we always use converter. */
        tv->cvt = secCvtCreate (pScrn, CVT_OP_M2M);
        XDBG_GOTO_IF_FAIL (tv->cvt != NULL, fail_connect);

        secCvtAddCallback (tv->cvt, _secVideoTvCvtCallback, tv);
    }
    else if (lpos == LAYER_LOWER1 && pSecMode->conn_mode == DISPLAY_CONN_MODE_HDMI)
    {
        if (!secLayerSupport (pScrn, LAYER_OUTPUT_EXT, lpos, id))
        {
            /* used if id is not supported in case of lpos == LAYER_LOWER1. */
            tv->cvt = secCvtCreate (pScrn, CVT_OP_M2M);
            XDBG_GOTO_IF_FAIL (tv->cvt != NULL, fail_connect);

            secCvtAddCallback (tv->cvt, _secVideoTvCvtCallback, tv);
        }
    }

    XDBG_DEBUG (MTVO, "id(%c%c%c%c), lpos(%d)!\n", FOURCC_STR (id), lpos);

    tv->pScrn = pScrn;
    tv->lpos = lpos;
    tv->outbuf_index = -1;
    tv->convert_id = FOURCC_RGB32;
    tv->outbuf_num = TVBUF_NUM;

    return tv;

fail_connect:
    if (tv)
    {
        if (tv->cvt)
            secCvtDestroy (tv->cvt);
        free (tv);
    }
    return NULL;
}

void
secVideoTvDisconnect (SECVideoTv* tv)
{
    int i;

    XDBG_RETURN_IF_FAIL (tv != NULL);

    XDBG_DEBUG (MTVO, "!\n");

    if (tv->cvt)
        secCvtDestroy (tv->cvt);

    _secVideoTvLayerDestroy (tv);

    if (tv->outbuf)
    {
        for (i = 0; i < tv->outbuf_num; i++)
            if (tv->outbuf[i])
                secUtilVideoBufferUnref (tv->outbuf[i]);

        free (tv->outbuf);
    }

    free (tv);
}

Bool
secVideoTvSetBuffer (SECVideoTv* tv, SECVideoBuf **vbufs, int bufnum)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (vbufs != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (bufnum >= TVBUF_NUM, FALSE);

    if (tv->outbuf)
    {
        XDBG_ERROR (MTVO, "already has buffers.\n");
        return FALSE;
    }

    tv->outbuf_num = bufnum;
    tv->outbuf = (SECVideoBuf**)calloc (bufnum, sizeof (SECVideoBuf*));
    XDBG_RETURN_VAL_IF_FAIL (tv->outbuf != NULL, FALSE);

    for (i = 0; i < tv->outbuf_num; i++)
    {
        XDBG_GOTO_IF_FAIL (tv->convert_id == vbufs[i]->id, fail_set_buffer);
        XDBG_GOTO_IF_FAIL (tv->tv_width == vbufs[i]->width, fail_set_buffer);
        XDBG_GOTO_IF_FAIL (tv->tv_height == vbufs[i]->height, fail_set_buffer);

        tv->outbuf[i] = secUtilVideoBufferRef (vbufs[i]);
        XDBG_GOTO_IF_FAIL (tv->outbuf[i] != NULL, fail_set_buffer);

        if (!tv->outbuf[i]->showing && tv->outbuf[i]->need_reset)
            secUtilClearVideoBuffer (tv->outbuf[i]);
        else
            tv->outbuf[i]->need_reset = TRUE;
    }

    return TRUE;

fail_set_buffer:
    if (tv->outbuf)
    {
        for (i = 0; i < tv->outbuf_num; i++)
        {
            if (tv->outbuf[i])
            {
                secUtilVideoBufferUnref (tv->outbuf[i]);
                tv->outbuf[i] = NULL;
            }
        }

        free (tv->outbuf);
        tv->outbuf = NULL;
    }

    return FALSE;
}
SECLayerPos
secVideoTvGetPos (SECVideoTv *tv)
{
    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, LAYER_NONE);

    return tv->lpos;
}

SECLayer*
secVideoTvGetLayer (SECVideoTv *tv)
{
    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, NULL);

    _secVideoTvLayerEnsure (tv);

    return tv->layer;
}

/* HDMI    : 'handles' is "gem handle"
 * VIRTUAL : 'handles' is "physical address"
 *           'data' is "raw data"
 *           only one of 'handles' and 'data' has value.
 */
int
secVideoTvPutImage (SECVideoTv *tv, SECVideoBuf *vbuf, xRectangle *rect, int csc_range)
{
    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, 0);
    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (vbuf), 0);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->id > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (rect != NULL, 0);

    XDBG_RETURN_VAL_IF_FAIL (vbuf->handles[0] > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->pitches[0] > 0, 0);

    _secVideoTvLayerEnsure (tv);
    XDBG_RETURN_VAL_IF_FAIL (tv->layer != NULL, 0);

    if (tv->cvt)
    {
        /* can't show buffer to HDMI at now. */
        SECModePtr pSecMode = (SECModePtr) SECPTR (tv->pScrn)->pSecMode;
        SECCvtProp src_prop = {0,}, dst_prop = {0,};
        SECVideoBuf *outbuf;
        int dst_width, dst_height;
        xRectangle dst_crop;


        /* CHECK */
        if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        {
            XDBG_RETURN_VAL_IF_FAIL (tv->tv_width > 0, 0);
            XDBG_RETURN_VAL_IF_FAIL (tv->tv_height > 0, 0);
            dst_width = tv->tv_width;
            dst_height = tv->tv_height;
            dst_crop = *rect;

            tv->tv_rect.x = 0;
            tv->tv_rect.y = 0;
            tv->tv_rect.width = tv->tv_width;
            tv->tv_rect.height = tv->tv_height;
        }
        else
        {
            dst_width = rect->width;
            dst_height = rect->height;
            dst_crop = *rect;
            dst_crop.x = 0;
            dst_crop.y = 0;

            tv->tv_rect = *rect;
        }

        src_prop.id = vbuf->id;
        src_prop.width = vbuf->width;
        src_prop.height = vbuf->height;
        src_prop.crop = vbuf->crop;

        dst_prop.id = tv->convert_id;
        dst_prop.width = dst_width;
        dst_prop.height = dst_height;
        dst_prop.crop = dst_crop;
        dst_prop.secure = vbuf->secure;
        dst_prop.csc_range = csc_range;

        if (!secCvtEnsureSize (&src_prop, &dst_prop))
            return 0;

        outbuf = _secVideoTvGetOutBuffer (tv, dst_prop.width, dst_prop.height, vbuf->secure);
        if (!outbuf)
            return 0;
        outbuf->crop = dst_prop.crop;

        if (!secCvtSetProperpty (tv->cvt, &src_prop, &dst_prop))
            return 0;

        if (!secCvtConvert (tv->cvt, vbuf, outbuf))
            return 0;

        XDBG_TRACE (MTVO, "'%c%c%c%c' %dx%d (%d,%d %dx%d) => '%c%c%c%c' %dx%d (%d,%d %dx%d) convert. rect(%d,%d %dx%d)\n",
                    FOURCC_STR (vbuf->id), vbuf->width, vbuf->height,
                    vbuf->crop.x, vbuf->crop.y, vbuf->crop.width, vbuf->crop.height,
                    FOURCC_STR (outbuf->id), outbuf->width, outbuf->height,
                    outbuf->crop.x, outbuf->crop.y, outbuf->crop.width, outbuf->crop.height,
                    rect->x, rect->y, rect->width, rect->height);

        return 1;
    }

    /* can show buffer to HDMI at now. */

    return _secVideoTvPutImageInternal (tv, vbuf, rect);
}

void
secVideoTvSetSize (SECVideoTv *tv, int width, int height)
{
    XDBG_RETURN_IF_FAIL (tv != NULL);

    tv->tv_width = width;
    tv->tv_height = height;

    XDBG_TRACE (MTVO, "size(%dx%d) \n", width, height);
}

SECCvt*
secVideoTvGetConverter (SECVideoTv *tv)
{
    XDBG_RETURN_VAL_IF_FAIL (tv != NULL, NULL);

    return tv->cvt;
}

void
secVideoTvSetConvertFormat (SECVideoTv *tv, unsigned int convert_id)
{
    XDBG_RETURN_IF_FAIL (tv != NULL);
    XDBG_RETURN_IF_FAIL (convert_id > 0);

    tv->convert_id = convert_id;

    XDBG_TRACE (MTVO, "convert_id(%c%c%c%c) \n", FOURCC_STR (convert_id));
}
