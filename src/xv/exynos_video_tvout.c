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

#include "exynos.h"
#include "exynos_display.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_accel.h"
#include "exynos_util.h"
#include "exynos_converter.h"
#include "exynos_video_tvout.h"
#include "exynos_video_virtual.h"
#include "exynos_video_fourcc.h"
#include "exynos_drm_ipp.h"
#include "exynos_layer.h"
#include "exynos_prop.h"

#include <sys/ioctl.h>
#include <exynos/exynos_drm.h>
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
struct _EXYNOSVideoTv {
    ScrnInfoPtr pScrn;

    EXYNOSLayer *layer;
    EXYNOSLayerPos lpos;

    /* used if id is not supported in case of lpos == LAYER_LOWER1. */
    EXYNOSCvt *cvt;
    EXYNOSVideoBuf **outbuf;
    int outbuf_index;
    int outbuf_num;

    int tv_width;
    int tv_height;

    xRectangle tv_rect;
    int is_resized;
    unsigned int convert_id;
    unsigned int src_id;
    /* attributes */
    int rotate;
    int hflip;
    int vflip;

    EXYNOSLayerOutput output;

};

static EXYNOSVideoBuf *
_exynosVideoTvGetOutBuffer(EXYNOSVideoTv * tv, int width, int height,
                           Bool exynosure)
{
    int i = tv->outbuf_index, j;

    if (!tv->outbuf) {
        tv->outbuf =
            (EXYNOSVideoBuf **) calloc(tv->outbuf_num,
                                       sizeof(EXYNOSVideoBuf *));
        XDBG_RETURN_VAL_IF_FAIL(tv->outbuf != NULL, NULL);
    }

    i++;
    if (i >= tv->outbuf_num)
        i = 0;

    for (j = 0; j < tv->outbuf_num; j++) {
        if (tv->outbuf[i]) {
            XDBG_DEBUG(MTVO, "outbuf(%p) converting(%d) showing(%d)\n",
                       tv->outbuf[i], VBUF_IS_CONVERTING(tv->outbuf[i]),
                       tv->outbuf[i]->showing);

            if (tv->outbuf[i] && !VBUF_IS_CONVERTING(tv->outbuf[i]) &&
                !tv->outbuf[i]->showing) {
                if (tv->outbuf[i]->width == width &&
                    tv->outbuf[i]->id == tv->convert_id &&
                    tv->outbuf[i]->height == height) {
                    tv->outbuf_index = i;
                    return tv->outbuf[i];
                }
                else {
                    exynosUtilVideoBufferUnref(tv->outbuf[i]);
                    EXYNOSPtr pExynos = EXYNOSPTR(tv->pScrn);

                    tv->outbuf[i] =
                        exynosUtilAllocVideoBuffer(tv->pScrn, tv->convert_id,
                                                   width, height,
                                                   (pExynos->
                                                    scanout) ? TRUE : FALSE,
                                                   TRUE, exynosure);
                    XDBG_RETURN_VAL_IF_FAIL(tv->outbuf[i] != NULL, NULL);

                    XDBG_DEBUG(MTVO, "outbuf(%p, %c%c%c%c)\n", tv->outbuf[i],
                               FOURCC_STR(tv->convert_id));

                    tv->outbuf_index = i;
                    return tv->outbuf[i];
                }
            }
        }
        else {
            EXYNOSPtr pExynos = EXYNOSPTR(tv->pScrn);

            tv->outbuf[i] =
                exynosUtilAllocVideoBuffer(tv->pScrn, tv->convert_id, width,
                                           height,
                                           (pExynos->scanout) ? TRUE : FALSE,
                                           TRUE, exynosure);
            XDBG_RETURN_VAL_IF_FAIL(tv->outbuf[i] != NULL, NULL);

            XDBG_DEBUG(MTVO, "outbuf(%p, %c%c%c%c)\n", tv->outbuf[i],
                       FOURCC_STR(tv->convert_id));

            tv->outbuf_index = i;
            return tv->outbuf[i];
        }

        i++;
        if (i >= tv->outbuf_num)
            i = 0;
    }

#if 0
    char buffers[1024];

    CLEAR(buffers);
    for (j = 0; j < tv->outbuf_num; j++) {
        if (tv->outbuf[j])
            snprintf(buffers, 1024, "%s %d(%d,%d) ", buffers,
                     tv->outbuf[j]->keys[0], VBUF_IS_CONVERTING(tv->outbuf[j]),
                     tv->outbuf[j]->showing);
    }

    XDBG_ERROR(MTVO, "now all outbufs in use! %s\n", buffers);
#else
    XDBG_ERROR(MTVO, "now all outbufs in use!\n");
#endif

    return NULL;
}

static Bool
_exynosVideoTvLayerEnsure(EXYNOSVideoTv * tv)
{
    EXYNOSLayer *layer;

    if (tv->layer)
        return TRUE;

    XDBG_RETURN_VAL_IF_FAIL(tv->lpos != LAYER_NONE, FALSE);

    layer = exynosLayerCreate(tv->pScrn, LAYER_OUTPUT_EXT, tv->lpos);
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);

    tv->layer = layer;

    return TRUE;
}

static void
_exynosVideoTvLayerDestroy(EXYNOSVideoTv * tv)
{
    if (tv->layer) {
        exynosLayerUnref(tv->layer);
        tv->layer = NULL;
    }
}

static int
_exynosVideoTvPutImageInternal(EXYNOSVideoTv * tv, EXYNOSVideoBuf * vbuf,
                               xRectangle *rect)
{
    int ret = 0;

    XDBG_DEBUG(MTVO, "rect (%d,%d %dx%d) \n",
               rect->x, rect->y, rect->width, rect->height);

    xRectangle src_rect, dst_rect;

    exynosLayerGetRect(tv->layer, &src_rect, &dst_rect);

    if (tv->is_resized == 1) {
        exynosLayerFreezeUpdate(tv->layer, FALSE);
        tv->is_resized = 0;
    }

    if (memcmp(&vbuf->crop, &src_rect, sizeof(xRectangle)) ||
        memcmp(rect, &dst_rect, sizeof(xRectangle))) {
        exynosLayerFreezeUpdate(tv->layer, TRUE);
        exynosLayerSetRect(tv->layer, &vbuf->crop, rect);
        exynosLayerFreezeUpdate(tv->layer, FALSE);
    }

    ret = exynosLayerSetBuffer(tv->layer, vbuf);

    if (ret == 0)
        return 0;

    exynosLayerShow(tv->layer);

    return ret;
}

static void
_exynosVideoTvCvtCallback(EXYNOSCvt * cvt,
                          EXYNOSVideoBuf * src,
                          EXYNOSVideoBuf * dst, void *cvt_data, Bool error)
{
    EXYNOSVideoTv *tv = (EXYNOSVideoTv *) cvt_data;
    int i;

    XDBG_RETURN_IF_FAIL(tv != NULL);
    XDBG_RETURN_IF_FAIL(cvt != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(dst));

    XDBG_DEBUG(MTVO, "++++++++++++++++++++++++ \n");

    for (i = 0; i < tv->outbuf_num; i++)
        if (tv->outbuf[i] == dst)
            break;
    XDBG_RETURN_IF_FAIL(i < tv->outbuf_num);
    _exynosVideoTvPutImageInternal(tv, dst, &tv->tv_rect);

    XDBG_DEBUG(MTVO, "++++++++++++++++++++++++.. \n");
}

EXYNOSVideoTv *
exynosVideoTvConnect(ScrnInfoPtr pScrn, unsigned int id, EXYNOSLayerPos lpos)
{
    EXYNOSVideoTv *tv = NULL;
    EXYNOSModePtr pExynosMode;
    unsigned int convert_id = FOURCC_RGB32;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL(lpos >= LAYER_LOWER1 && lpos <= LAYER_UPPER, NULL);
    XDBG_RETURN_VAL_IF_FAIL(id > 0, NULL);

    tv = calloc(sizeof(EXYNOSVideoTv), 1);
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, NULL);

    pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;

    if (lpos == LAYER_LOWER1 &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
        /* In case of video-only(virtual), we always use converter. */
        tv->cvt = exynosCvtCreate(pScrn, CVT_OP_M2M);
        XDBG_GOTO_IF_FAIL(tv->cvt != NULL, fail_connect);

        exynosCvtAddCallback(tv->cvt, _exynosVideoTvCvtCallback, tv);
    }
    else if (lpos == LAYER_LOWER1 &&
             pExynosMode->conn_mode == DISPLAY_CONN_MODE_HDMI) {
        if (!exynosLayerSupport(pScrn, LAYER_OUTPUT_EXT, lpos, id)) {
            /* used if id is not supported in case of lpos == LAYER_LOWER1. */
            convert_id = FOURCC_SN12;
            tv->cvt = exynosCvtCreate(pScrn, CVT_OP_M2M);
            XDBG_GOTO_IF_FAIL(tv->cvt != NULL, fail_connect);
            exynosCvtAddCallback(tv->cvt, _exynosVideoTvCvtCallback, tv);
        }
        else {
            XDBG_DEBUG(MTVO, "Not need converting id(%c%c%c%c)\n",
                       FOURCC_STR(id));
            convert_id = id;
        }
    }

    XDBG_DEBUG(MTVO, "id(%c%c%c%c), lpos(%d)!\n", FOURCC_STR(id), lpos);
    tv->output = LAYER_OUTPUT_EXT;
    tv->pScrn = pScrn;
    tv->lpos = lpos;
    tv->outbuf_index = -1;
    tv->convert_id = convert_id;
    tv->outbuf_num = TVBUF_NUM;
    tv->src_id = id;

    return tv;

 fail_connect:
    if (tv) {
        if (tv->cvt)
            exynosCvtDestroy(tv->cvt);
        free(tv);
    }
    return NULL;
}

Bool
exynosVideoTvResizeOutput(EXYNOSVideoTv * tv, xRectanglePtr src,
                          xRectanglePtr dst)
{
    if (tv == NULL)
        return FALSE;

    if (!exynosVideoTvCanDirectDrawing(tv, src->width, src->height,
                                       dst->width, dst->height)) {
        if (tv->cvt) {
            XDBG_DEBUG(MTVO, "Pause converter\n");
            if (!exynosCvtPause(tv->cvt)) {
                XDBG_ERROR(MTVO, "Can't pause ipp converter\n");
                exynosVideoTvReCreateConverter(tv);
            }
        }
        else {
            XDBG_DEBUG(MTVO, "Create new converter tasks\n");
            exynosVideoTvReCreateConverter(tv);
        }
    }
    else {
        if (tv->cvt) {
            XDBG_DEBUG(MTVO, "Driver can use direct drawing way.\n");
            exynosCvtDestroy(tv->cvt);
            tv->cvt = NULL;
        }
    }

    if (tv->outbuf) {
        int i;

        for (i = 0; i < tv->outbuf_num; i++)
            if (tv->outbuf[i] && !VBUF_IS_CONVERTING(tv->outbuf[i]) &&
                !tv->outbuf[i]->showing) {
                exynosUtilVideoBufferUnref(tv->outbuf[i]);
                tv->outbuf[i] = NULL;
            }
        tv->outbuf_index = -1;
    }

    exynosLayerFreezeUpdate(tv->layer, TRUE);
    tv->is_resized = 1;
    return TRUE;
}

void
exynosVideoTvDisconnect(EXYNOSVideoTv * tv)
{
    int i;

    XDBG_RETURN_IF_FAIL(tv != NULL);

    XDBG_DEBUG(MTVO, "!\n");

    if (tv->cvt)
        exynosCvtDestroy(tv->cvt);

    _exynosVideoTvLayerDestroy(tv);

    if (tv->outbuf) {
        for (i = 0; i < tv->outbuf_num; i++)
            if (tv->outbuf[i])
                exynosUtilVideoBufferUnref(tv->outbuf[i]);

        free(tv->outbuf);
    }

    free(tv);
}

Bool
exynosVideoTvSetBuffer(EXYNOSVideoTv * tv, EXYNOSVideoBuf ** vbufs, int bufnum)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(vbufs != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(bufnum >= TVBUF_NUM, FALSE);

    if (tv->outbuf) {
        XDBG_ERROR(MTVO, "already has buffers.\n");
        return FALSE;
    }

    tv->outbuf_num = bufnum;
    tv->outbuf = (EXYNOSVideoBuf **) calloc(bufnum, sizeof(EXYNOSVideoBuf *));
    XDBG_RETURN_VAL_IF_FAIL(tv->outbuf != NULL, FALSE);

    for (i = 0; i < tv->outbuf_num; i++) {
        XDBG_GOTO_IF_FAIL(tv->convert_id == vbufs[i]->id, fail_set_buffer);
        XDBG_GOTO_IF_FAIL(tv->tv_width == vbufs[i]->width, fail_set_buffer);
        XDBG_GOTO_IF_FAIL(tv->tv_height == vbufs[i]->height, fail_set_buffer);

        tv->outbuf[i] = exynosUtilVideoBufferRef(vbufs[i]);
        XDBG_GOTO_IF_FAIL(tv->outbuf[i] != NULL, fail_set_buffer);

        if (!tv->outbuf[i]->showing && tv->outbuf[i]->need_reset)
            exynosUtilClearVideoBuffer(tv->outbuf[i]);
        else
            tv->outbuf[i]->need_reset = TRUE;
    }

    return TRUE;

 fail_set_buffer:
    if (tv->outbuf) {
        for (i = 0; i < tv->outbuf_num; i++) {
            if (tv->outbuf[i]) {
                exynosUtilVideoBufferUnref(tv->outbuf[i]);
                tv->outbuf[i] = NULL;
            }
        }

        free(tv->outbuf);
        tv->outbuf = NULL;
    }

    return FALSE;
}

EXYNOSLayerPos
exynosVideoTvGetPos(EXYNOSVideoTv * tv)
{
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, LAYER_NONE);

    return tv->lpos;
}

EXYNOSLayer *
exynosVideoTvGetLayer(EXYNOSVideoTv * tv)
{
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, NULL);

    _exynosVideoTvLayerEnsure(tv);

    return tv->layer;
}

/* HDMI    : 'handles' is "gem handle"
 * VIRTUAL : 'handles' is "physical address"
 *           'data' is "raw data"
 *           only one of 'handles' and 'data' has value.
 */
int
exynosVideoTvPutImage(EXYNOSVideoTv * tv, EXYNOSVideoBuf * vbuf,
                      xRectangle *rect, int csc_range)
{
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, 0);
    XDBG_RETURN_VAL_IF_FAIL(VBUF_IS_VALID(vbuf), 0);
    XDBG_RETURN_VAL_IF_FAIL(vbuf->id > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(rect != NULL, 0);

    XDBG_RETURN_VAL_IF_FAIL(vbuf->handles[0] > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(vbuf->pitches[0] > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(rect->width > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(rect->height > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(vbuf->width > 0, 0);
    XDBG_RETURN_VAL_IF_FAIL(vbuf->height > 0, 0);
    _exynosVideoTvLayerEnsure(tv);
    XDBG_RETURN_VAL_IF_FAIL(tv->layer != NULL, 0);

    if (tv->cvt) {
        /* can't show buffer to HDMI at now. */

        EXYNOSCvtProp src_prop = { 0, }
        , dst_prop = {
        0,};
        EXYNOSVideoBuf *outbuf;
        int dst_width, dst_height;
        xRectangle dst_crop;

#if 0
        /* CHECK */
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(tv->pScrn)->pExynosMode;

        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
            XDBG_RETURN_VAL_IF_FAIL(tv->tv_width > 0, 0);
            XDBG_RETURN_VAL_IF_FAIL(tv->tv_height > 0, 0);
            dst_width = tv->tv_width;
            dst_height = tv->tv_height;
            dst_crop = *rect;

            tv->tv_rect.x = 0;
            tv->tv_rect.y = 0;
            tv->tv_rect.width = tv->tv_width;
            tv->tv_rect.height = tv->tv_height;
        }
        else
#endif
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
        dst_prop.exynosure = vbuf->exynosure;
        dst_prop.csc_range = csc_range;
        dst_prop.hflip = tv->hflip;
        dst_prop.vflip = tv->vflip;
        dst_prop.degree = tv->rotate;
        if (!exynosCvtEnsureSize(&src_prop, &dst_prop)) {
            XDBG_ERROR(MTVO, "Can't ensure size\n");
            return 0;
        }

        outbuf =
            _exynosVideoTvGetOutBuffer(tv, dst_prop.width, dst_prop.height,
                                       vbuf->exynosure);
        if (!outbuf) {
            XDBG_ERROR(MTVO, "Can't get outbuf\n");
            return 0;
        }
        outbuf->crop = dst_prop.crop;

        if (!exynosCvtSetProperpty(tv->cvt, &src_prop, &dst_prop)) {
            XDBG_ERROR(MTVO, "Can't set cvt property\n");
            return 0;
        }

        if (!exynosCvtConvert(tv->cvt, vbuf, outbuf)) {
            XDBG_ERROR(MTVO, "Can't start cvt\n");
            return 0;
        }

        XDBG_TRACE(MTVO,
                   "'%c%c%c%c' %dx%d (%d,%d %dx%d) => '%c%c%c%c' %dx%d (%d,%d %dx%d) convert. rect(%d,%d %dx%d)\n",
                   FOURCC_STR(vbuf->id), vbuf->width, vbuf->height,
                   vbuf->crop.x, vbuf->crop.y, vbuf->crop.width,
                   vbuf->crop.height, FOURCC_STR(outbuf->id), outbuf->width,
                   outbuf->height, outbuf->crop.x, outbuf->crop.y,
                   outbuf->crop.width, outbuf->crop.height, rect->x, rect->y,
                   rect->width, rect->height);

        return 1;
    }
    /* can show buffer to HDMI at now. */
    return _exynosVideoTvPutImageInternal(tv, vbuf, rect);
}

void
exynosVideoTvSetSize(EXYNOSVideoTv * tv, int width, int height)
{
    XDBG_RETURN_IF_FAIL(tv != NULL);

    tv->tv_width = width;
    tv->tv_height = height;

    XDBG_TRACE(MTVO, "size(%dx%d) \n", width, height);
}

EXYNOSCvt *
exynosVideoTvGetConverter(EXYNOSVideoTv * tv)
{
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, NULL);

    return tv->cvt;
}

void
exynosVideoTvSetConvertFormat(EXYNOSVideoTv * tv, unsigned int convert_id)
{
    XDBG_RETURN_IF_FAIL(tv != NULL);
    XDBG_RETURN_IF_FAIL(convert_id > 0);

    tv->convert_id = convert_id;

    XDBG_TRACE(MTVO, "convert_id(%c%c%c%c) \n", FOURCC_STR(convert_id));
}

Bool
exynosVideoTvCanDirectDrawing(EXYNOSVideoTv * tv, int src_w, int src_h,
                              int dst_w, int dst_h)
{
    XDBG_RETURN_VAL_IF_FAIL(src_w > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src_h > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_w > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_h > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(tv != 0, FALSE);
    int ratio_w = 0;
    int ratio_h = 0;

    XDBG_DEBUG(MTVO, "tv(%p) src_w %d, src_h %d, dst_w %d, dst_h %d\n",
               tv, src_w, src_h, dst_w, dst_h);
    if (tv->hflip != 0 || tv->vflip != 0 || tv->rotate != 0) {
        XDBG_DEBUG(MTVO, "Can't direct draw hflip(%d), vflip(%d), rotate(%d)\n",
                   tv->hflip, tv->vflip, tv->rotate);
        return FALSE;
    }
    if (src_w >= dst_w) {
        ratio_w = src_w / dst_w;
        if (ratio_w > 4) {
            XDBG_DEBUG(MTVO, "Can't direct draw ratio_w (1/%d) < 1/4\n",
                       ratio_w);
            return FALSE;
        }
        XDBG_DEBUG(MTVO, "ratio_w = 1/%d\n", ratio_w);
    }
    else if (src_w < dst_w) {
        ratio_w = dst_w / src_w;
        if (ratio_w > 16) {
            XDBG_DEBUG(MTVO, "Can't direct draw ratio_w (%d) > 16\n", ratio_w);
            return FALSE;
        }
        XDBG_DEBUG(MTVO, "ratio_w = %d\n", ratio_w);
    }

    if (src_h >= dst_h) {
        ratio_h = src_h / dst_h;
        if (ratio_h > 4) {
            XDBG_DEBUG(MTVO, "Can't direct draw ratio_h (1/%d) < 1/4\n",
                       ratio_w);
            return FALSE;
        }
        XDBG_DEBUG(MTVO, "ratio_h = 1/%d\n", ratio_h);
    }
    else if (src_h < dst_h) {
        ratio_h = dst_h / src_h;
        if (ratio_h > 16) {
            XDBG_DEBUG(MTVO, "Can't direct draw ratio_w (%d) > 16\n", ratio_w);
            return FALSE;
        }
        XDBG_DEBUG(MTVO, "ratio_h = %d\n", ratio_h);
    }

    if (!exynosLayerSupport(tv->pScrn, tv->output, tv->lpos, tv->src_id)) {
        XDBG_DEBUG(MTVO,
                   "Can't direct draw. Layer not support. lpos(%d), src_id (%c%c%c%c)\n",
                   tv->lpos, FOURCC_STR(tv->src_id));
        return FALSE;
    }

    XDBG_DEBUG(MTVO, "Support direct drawing\n");
    return TRUE;
}

Bool
exynosVideoTvReCreateConverter(EXYNOSVideoTv * tv)
{
    if (tv == NULL)
        return FALSE;
    if (tv->cvt) {
        exynosCvtDestroy(tv->cvt);
    }
    tv->cvt = exynosCvtCreate(tv->pScrn, CVT_OP_M2M);
    XDBG_RETURN_VAL_IF_FAIL(tv->cvt != NULL, FALSE);
    exynosCvtAddCallback(tv->cvt, _exynosVideoTvCvtCallback, tv);
    return TRUE;
}

Bool
exynosVideoTvSetAttributes(EXYNOSVideoTv * tv, int rotate, int hflip, int vflip)
{
    XDBG_RETURN_VAL_IF_FAIL(tv != NULL, FALSE);
    tv->rotate = rotate;
    tv->vflip = vflip;
    tv->hflip = hflip;
    return TRUE;
}
