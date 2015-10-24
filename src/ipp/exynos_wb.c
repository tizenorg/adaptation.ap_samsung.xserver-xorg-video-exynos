/*
 * xserver-xorg-video-exynos
 *
 * Copyright 2004 Keith Packard
 * Copyright 2005 Eric Anholt
 * Copyright 2006 Nokia Corporation
 * Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.
 *
 * Contact: Boram Park <boram1288.park@samsung.com>
 *
 * Permission to use, copy, modify, distribute and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the names of the authors and/or copyright holders
 * not be used in advertising or publicity pertaining to distribution of the
 * software without specific, written prior permission.  The authors and
 * copyright holders make no representations about the suitability of this
 * software for any purpose.  It is provided "as is" without any express
 * or implied warranty.
 *
 * THE AUTHORS AND COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO
 * THIS SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR
 * ANY SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/time.h>

#include <X11/Xatom.h>
#include <xace.h>
#include <xacestr.h>

#include <exynos/exynos_drm.h>

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_crtc.h"
#include "exynos_video_fourcc.h"
#include "exynos_video_tvout.h"
#include "exynos_wb.h"
#include "exynos_drm_ipp.h"
#include "exynos_converter.h"

#include <drm_fourcc.h>

#define WB_BUF_MAX      5
#define WB_BUF_DEFAULT  3
#define WB_BUF_MIN      2

enum {
    PENDING_NONE,
    PENDING_ROTATE,
    PENDING_STOP,
    PENDING_CLOSE,
};

enum {
    STATUS_RUNNING,
    STATUS_PAUSE,
    STATUS_STOP,
};

typedef struct _EXYNOSWbNotifyFuncInfo {
    EXYNOSWbNotify noti;
    WbNotifyFunc func;
    void *user_data;

    struct _EXYNOSWbNotifyFuncInfo *next;
} EXYNOSWbNotifyFuncInfo;

struct _EXYNOSWb {
    int prop_id;

    ScrnInfoPtr pScrn;

    unsigned int id;

    int rotate;

    int width;
    int height;
    xRectangle drm_dst;
    xRectangle tv_dst;

    EXYNOSVideoBuf *dst_buf[WB_BUF_MAX];
    Bool queued[WB_BUF_MAX];
    int buf_num;

    int wait_show;
    int now_showing;

    EXYNOSWbNotifyFuncInfo *info_list;

    /* for tvout */
    Bool tvout;
    EXYNOSVideoTv *tv;
    EXYNOSLayerPos lpos;

    Bool need_rotate_hook;
    OsTimerPtr rotate_timer;

    /* count */
    unsigned int put_counts;
    unsigned int last_counts;
    OsTimerPtr timer;

    OsTimerPtr event_timer;
    OsTimerPtr ipp_timer;

    Bool scanout;
    int hz;

    int status;
    Bool exynosure;
    CARD32 prev_time;
};

static unsigned int formats[] = {
    FOURCC_RGB32,
    FOURCC_ST12,
    FOURCC_SN12,
};

static EXYNOSWb *keep_wb;
static Atom atom_wb_rotate;

static void _exynosWbQueue(EXYNOSWb * wb, int index);
static Bool _exynosWbRegisterRotateHook(EXYNOSWb * wb, Bool hook);
static void _exynosWbCloseDrmDstBuffer(EXYNOSWb * wb);
static void _exynosWbCloseDrm(EXYNOSWb * wb, Bool pause);

static CARD32
_exynosWbCountPrint(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_DEBUG(MWB, "E. timer %p now %" PRIXID " arg %p\n", timer, now, arg);
    EXYNOSWb *wb = (EXYNOSWb *) arg;

    ErrorF("IppEvent : %d fps. \n", wb->put_counts - wb->last_counts);

    wb->last_counts = wb->put_counts;

    wb->timer = TimerSet(wb->timer, 0, 1000, _exynosWbCountPrint, arg);
    XDBG_DEBUG(MWB, "Q. wb->timer = %p. ret 0\n", wb->timer);
    return 0;
}

static void
_exynosWbCountFps(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    wb->put_counts++;

    if (wb->timer) {
        XDBG_DEBUG(MWB, "Q. wb->timer == %p\n", wb->timer);
        return;
    }
    wb->timer = TimerSet(NULL, 0, 1000, _exynosWbCountPrint, wb);
    XDBG_DEBUG(MWB, "Q. wb->timer = %p\n", wb->timer);
}

static void
_exynosWbCountStop(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (wb->timer) {
        TimerFree(wb->timer);
        wb->timer = NULL;
    }

    wb->put_counts = 0;
    wb->last_counts = 0;
    XDBG_DEBUG(MWB, "Q.\n");
}

static unsigned int
_exynosWbSupportFormat(int id)
{
    XDBG_DEBUG(MWB, "E. id %d\n", id);
    unsigned int *drmfmts;
    int i, size, num = 0;
    unsigned int drmfmt = exynosUtilGetDrmFormat(id);

    size = sizeof(formats) / sizeof(unsigned int);

    for (i = 0; i < size; i++)
        if (formats[i] == id)
            break;

    if (i == size) {
        XDBG_ERROR(MWB, "wb not support : '%c%c%c%c'.\n", FOURCC_STR(id));
        XDBG_DEBUG(MWB, "Q. i(%d) == size(%d). ret 0\n", i, size);
        return 0;
    }

    drmfmts = exynosDrmIppGetFormatList(&num);
    if (!drmfmts) {
        XDBG_ERROR(MWB, "no drm format list.\n");
        XDBG_DEBUG(MWB, "Q. drmfmts(%p) == NULL. ret 0\n", drmfmts);
        return 0;
    }

    for (i = 0; i < num; i++)
        if (drmfmts[i] == drmfmt) {
            free(drmfmts);
            XDBG_DEBUG(MWB, "Q. id(%d) is support. ret %d(drmfmt)\n", id,
                       drmfmt);
            return drmfmt;
        }

    XDBG_ERROR(MWB, "drm ipp not support : '%c%c%c%c'.\n", FOURCC_STR(id));

    free(drmfmts);
    XDBG_DEBUG(MWB, "Q. id %d not found in support format list. ret 0\n", id);
    return 0;
}

static void
_exynosWbCallNotifyFunc(EXYNOSWb * wb, EXYNOSWbNotify noti, void *noti_data)
{
    XDBG_DEBUG(MWB, "E. wb %p noti %d noti_data %p\n", wb, noti, noti_data);
    EXYNOSWbNotifyFuncInfo *info;

    nt_list_for_each_entry(info, wb->info_list, next) {
        if (info->noti == noti && info->func)
            info->func(wb, noti, noti_data, info->user_data);
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

static void
_exynosWbLayerNotifyFunc(EXYNOSLayer * layer, int type, void *type_data,
                         void *data)
{
    XDBG_DEBUG(MWB, "E. layer %p type %d type_data %p data %p\n", layer, type,
               type_data, data);
    EXYNOSWb *wb = (EXYNOSWb *) data;
    EXYNOSVideoBuf *vbuf = (EXYNOSVideoBuf *) type_data;

    if (type != LAYER_VBLANK) {
        XDBG_DEBUG(MWB, "Q. type(%d) != LAYER_VBLANK\n", type);
        return;
    }
    XDBG_RETURN_IF_FAIL(wb != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(vbuf));

    if (wb->status == STATUS_PAUSE) {
        XDBG_WARNING(MWB, "pause status. return.\n");
        XDBG_DEBUG(MWB, "Q. wb->status == STATUS_PAUSE\n");
        return;
    }

    if (wb->wait_show >= 0 && wb->dst_buf[wb->wait_show] != vbuf)
        XDBG_WARNING(MWB, "wait_show(%d,%p) != showing_vbuf(%p). \n",
                     wb->wait_show, wb->dst_buf[wb->wait_show], vbuf);

    if (wb->now_showing >= 0)
        _exynosWbQueue(wb, wb->now_showing);

    wb->now_showing = wb->wait_show;
    wb->wait_show = -1;

    XDBG_TRACE(MWB, "now_showing(%d,%p). \n", wb->now_showing, vbuf);
    XDBG_DEBUG(MWB, "Q.\n");
}

static Bool
_exynosWbCalTvoutRect(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(wb->pScrn)->pExynosMode;
    int src_w, src_h, dst_w, dst_h;

    if (!wb->tvout) {
        if (wb->width == 0 || wb->height == 0) {
            wb->width = pExynosMode->main_lcd_mode.hdisplay;
            wb->height = pExynosMode->main_lcd_mode.vdisplay;
        }

        src_w = pExynosMode->main_lcd_mode.hdisplay;
        src_h = pExynosMode->main_lcd_mode.vdisplay;
        dst_w = wb->width;
        dst_h = wb->height;
        if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
            XDBG_ERROR(MWB, "Error. Receive Negative size\n");
            XDBG_DEBUG(MWB,
                       "Q. src_w(%d) <= 0 || src_h(%d) <= 0 || dst_w(%d) <= 0 || dst_h(%d) <= 0. ret FALSE\n",
                       src_w, src_h, dst_w, dst_h);
            return FALSE;
        }

        if (wb->rotate % 180)
            SWAP(src_w, src_h);

        exynosUtilAlignRect(src_w, src_h, dst_w, dst_h, &wb->drm_dst, TRUE);
    }
    else {
        src_w = (int) pExynosMode->main_lcd_mode.hdisplay;
        src_h = (int) pExynosMode->main_lcd_mode.vdisplay;
        dst_w = (int) pExynosMode->ext_connector_mode.hdisplay;
        dst_h = (int) pExynosMode->ext_connector_mode.vdisplay;
        if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) {
            XDBG_ERROR(MWB, "Error. Receive Negative size\n");
            XDBG_DEBUG(MWB,
                       "Q. src_w(%d) <= 0 || src_h(%d) <= 0 || dst_w(%d) <= 0 || dst_h(%d) <= 0. ret FALSE\n",
                       src_w, src_h, dst_w, dst_h);
            return FALSE;
        }

        if (wb->rotate % 180)
            SWAP(src_w, src_h);
#if 0
        if (wb->lpos == LAYER_UPPER) {
            /* Mixer can't scale. */
            wb->width = dst_w;
            wb->height = dst_h;
            wb->tv_dst.width = wb->width;
            wb->tv_dst.height = wb->height;

            exynosUtilAlignRect(src_w, src_h, dst_w, dst_h, &wb->drm_dst, TRUE);
        }
        else {                  /* LAYER_LOWER1 */

            /* VP can scale */
            exynosUtilAlignRect(src_w, src_h, dst_w, dst_h, &wb->tv_dst, TRUE);

            wb->width = src_w;
            wb->height = src_h;

            wb->drm_dst.width = wb->width;
            wb->drm_dst.height = wb->height;
        }
#else
        /* Mixer can't scale. */
        wb->width = dst_w;
        wb->height = dst_h;
        wb->tv_dst.width = wb->width;
        wb->tv_dst.height = wb->height;

        exynosUtilAlignRect(src_w, src_h, dst_w, dst_h, &wb->drm_dst, TRUE);
#endif
    }

    XDBG_TRACE(MWB,
               "tvout(%d) lpos(%d) src(%dx%d) drm_dst(%d,%d %dx%d) tv_dst(%d,%d %dx%d).\n",
               wb->tvout, wb->lpos, wb->width, wb->height, wb->drm_dst.x,
               wb->drm_dst.y, wb->drm_dst.width, wb->drm_dst.height,
               wb->tv_dst.x, wb->tv_dst.y, wb->tv_dst.width, wb->tv_dst.height);
    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;
}

static void
_exynosWbQueue(EXYNOSWb * wb, int index)
{
    XDBG_DEBUG(MWB, "E. wb %p index %d\n", wb, index);
    struct drm_exynos_ipp_queue_buf buf;
    int j;

    if (index < 0) {
        XDBG_DEBUG(MWB, "Q. index(%d) < 0\n", index);
        return;
    }

    if (wb->dst_buf[index]->showing == TRUE) {
        XDBG_DEBUG(MWB, "Q. wb->dst_buf[$d]->showing == TRUE\n", index);
        return;
    }
    CLEAR(buf);
    buf.ops_id = EXYNOS_DRM_OPS_DST;
    buf.buf_type = IPP_BUF_ENQUEUE;
    buf.prop_id = wb->prop_id;
    buf.buf_id = index;
    buf.user_data = (__u64) (uintptr_t) wb;

    for (j = 0; j < PLANAR_CNT; j++)
        buf.handle[j] = wb->dst_buf[index]->handles[j];

    if (!exynosDrmIppQueueBuf(wb->pScrn, &buf)) {
        XDBG_DEBUG(MWB, "Q. exynosDrmIppQueueBuf == FALSE\n");
        return;
    }

    wb->queued[index] = TRUE;
    wb->dst_buf[index]->dirty = TRUE;

    XDBG_TRACE(MWB, "index(%d)\n", index);
    XDBG_DEBUG(MWB, "Q.\n");
}

static int
_exynosWbDequeued(EXYNOSWb * wb, Bool skip_put, int index)
{
    XDBG_DEBUG(MWB, "E. wb %p skip_put %s index %d\n", wb,
               skip_put ? "TRUE" : "FALSE", index);
    int i, remain = 0;

    if (index >= wb->buf_num) {
        XDBG_ERROR(MWB, "Wrong buf index\n");
        XDBG_DEBUG(MWB, "Q. index(%d) >= wb->buf_num($d). ret -1\n", index,
                   wb->buf_num);
        return -1;
    }
    XDBG_WARNING_IF_FAIL(wb->dst_buf[index]->showing == FALSE);

    XDBG_TRACE(MWB, "skip_put(%d) index(%d)\n", skip_put, index);

    if (!wb->queued[index])
        XDBG_WARNING(MWB, "buf(%d) already dequeued.\n", index);

    wb->queued[index] = FALSE;

    for (i = 0; i < wb->buf_num; i++) {
        if (wb->queued[i])
            remain++;
    }

    /* the count of remain buffers should be more than 2. */
    if (remain >= WB_BUF_MIN)
        _exynosWbCallNotifyFunc(wb, WB_NOTI_IPP_EVENT,
                                (void *) wb->dst_buf[index]);
    else
        XDBG_TRACE(MWB, "remain buffer count: %d\n", remain);

    if (wb->tvout) {
        if (!wb->tv) {
            if (wb->tv_dst.width > 0 && wb->tv_dst.height > 0)
                wb->tv = exynosVideoTvConnect(wb->pScrn, wb->id, wb->lpos);

            if (wb->tv && !exynosUtilEnsureExternalCrtc(wb->pScrn)) {
                wb->tvout = FALSE;
                exynosVideoTvDisconnect(wb->tv);
                wb->tv = NULL;
            }

            if (wb->tv) {
                EXYNOSLayer *layer = exynosVideoTvGetLayer(wb->tv);

                exynosLayerEnableVBlank(layer, TRUE);
                exynosLayerAddNotifyFunc(layer, _exynosWbLayerNotifyFunc, wb);
            }
        }

        if (!skip_put && wb->tv) {
            wb->wait_show = index;
            exynosVideoTvPutImage(wb->tv, wb->dst_buf[index], &wb->tv_dst, 0);
        }
    }

    if (remain == 0)
        XDBG_ERROR(MWB, "*** wb's buffer empty!! *** \n");

    XDBG_TRACE(MWB, "tv(%p) wait_show(%d) remain(%d)\n", wb->tv,
               wb->wait_show, remain);
    XDBG_DEBUG(MWB, "Q. ret %d(index)\n", index);
    return index;
}

static CARD32
_exynosWbIppRetireTimeout(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_DEBUG(MWB, "E. timer %p now %" PRIXID " arg %p\n", timer, now, arg);
    EXYNOSWb *wb = (EXYNOSWb *) arg;

    if (wb->ipp_timer) {
        TimerFree(wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    XDBG_ERROR(MWB, "failed : +++ WB IPP Retire Timeout!!\n");
    XDBG_DEBUG(MWB, "Q. ret 0\n");
    return 0;
}

unsigned int
exynosWbGetPropID(void)
{
    XDBG_DEBUG(MWB, "E.\n");
    if (!keep_wb) {
        XDBG_DEBUG(MWB, "Q. keep_wb == NULL. ret -1\n");
        return -1;
    }
    XDBG_DEBUG(MWB, "Q. ret %d(prop_id)\n", keep_wb->prop_id);
    return keep_wb->prop_id;
}

void
exynosWbHandleIppEvent(int fd, unsigned int *buf_idx, void *data)
{
    XDBG_DEBUG(MWB, "E. fd %d buf_idx %p data %p\n", fd, buf_idx, data);
    EXYNOSWb *wb = (EXYNOSWb *) data;
    EXYNOSPtr pExynos;
    int index = buf_idx[EXYNOS_DRM_OPS_DST];

    if (!wb) {
        XDBG_ERROR(MWB, "Error. receive NULL data\n");
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }

    if (keep_wb != wb) {
        XDBG_WARNING(MWB, "Useless ipp event! (%p) \n", wb);
        XDBG_DEBUG(MWB, "Q. wb(%p) != keep_wb(%p)\n\n", wb, keep_wb);
        return;
    }

    if (wb->event_timer) {
        TimerFree(wb->event_timer);
        wb->event_timer = NULL;
    }

    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE)) {
        XDBG_ERROR(MWB, "stop or pause. ignore a event. %p, (status:%d)\n",
                   data, wb->status);
        XDBG_DEBUG(MWB, "Q. wb->status == %s\n",
                   (wb->status ==
                    STATUS_STOP) ? "STATUS_STOP" : "STATUS_PAUSE");
        return;
    }

    if (wb->ipp_timer) {
        TimerFree(wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    wb->ipp_timer =
        TimerSet(wb->ipp_timer, 0, 2000, _exynosWbIppRetireTimeout, wb);

    XDBG_TRACE(MWB, "=============== wb(%p) !\n", wb);

    pExynos = EXYNOSPTR(wb->pScrn);

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_WB) {
        CARD32 cur, sub;

        cur = GetTimeInMillis();
        sub = cur - wb->prev_time;
        wb->prev_time = cur;
        ErrorF("wb evt interval  : %6" PRIXID " ms\n", sub);
    }

    if (pExynos->wb_fps)
        _exynosWbCountFps(wb);
    else
        _exynosWbCountStop(wb);

    if (wb->rotate_timer) {
        _exynosWbDequeued(wb, TRUE, index);
        _exynosWbQueue(wb, index);
    }
    else {
        if (wb->wait_show >= 0) {
            _exynosWbDequeued(wb, TRUE, index);
            _exynosWbQueue(wb, index);
        }
        else {
            _exynosWbDequeued(wb, FALSE, index);

            if (wb->wait_show < 0 && !wb->dst_buf[index]->showing)
                _exynosWbQueue(wb, index);
        }
    }

    _exynosWbCallNotifyFunc(wb, WB_NOTI_IPP_EVENT_DONE, NULL);

    XDBG_TRACE(MWB, "=============== !\n");
    XDBG_DEBUG(MWB, "Q.\n");
}

static Bool
_exynosWbEnsureDrmDstBuffer(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    int i;

    for (i = 0; i < wb->buf_num; i++) {
        if (wb->dst_buf[i]) {
            exynosUtilClearVideoBuffer(wb->dst_buf[i]);
            continue;
        }

        wb->dst_buf[i] = exynosUtilAllocVideoBuffer(wb->pScrn, wb->id,
                                                    wb->width, wb->height,
                                                    keep_wb->scanout, TRUE,
                                                    exynosVideoIsSecureMode
                                                    (wb->pScrn));
        XDBG_GOTO_IF_FAIL(wb->dst_buf[i] != NULL, fail_to_ensure);
    }
    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;
 fail_to_ensure:
    _exynosWbCloseDrmDstBuffer(wb);
    XDBG_DEBUG(MWB, "Q. ret FALSE\n");
    return FALSE;
}

static void
_exynosWbCloseDrmDstBuffer(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    int i;

    for (i = 0; i < wb->buf_num; i++) {
        if (wb->dst_buf[i]) {
            exynosUtilVideoBufferUnref(wb->dst_buf[i]);
            wb->dst_buf[i] = NULL;
        }
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

static void
_exynosWbClearDrmDstBuffer(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    int i;

    for (i = 0; i < wb->buf_num; i++) {
        if (wb->dst_buf[i]) {
            if (!wb->dst_buf[i]->showing && wb->dst_buf[i]->need_reset)
                exynosUtilClearVideoBuffer(wb->dst_buf[i]);
            else
                wb->dst_buf[i]->need_reset = TRUE;
        }
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

static CARD32
_exynosWbEventTimeout(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_DEBUG(MWB, "E. timer %p now %" PRIXID " arg %p\n", timer, now, arg);
    EXYNOSWb *wb = (EXYNOSWb *) arg;

    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret 0\n");
        return 0;
    }

    if (wb->event_timer) {
        TimerFree(wb->event_timer);
        wb->event_timer = NULL;
    }

    XDBG_ERROR(MWB, "*** ipp event not happen!! \n");
    XDBG_DEBUG(MWB, "E. ret 0\n");
    return 0;
}

static Bool
_exynosWbOpenDrm(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    EXYNOSPtr pExynos = EXYNOSPTR(wb->pScrn);
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) pExynos->pExynosMode;
    int i;
    unsigned int drmfmt = 0;
    struct drm_exynos_ipp_property property;
    enum drm_exynos_degree degree;
    struct drm_exynos_ipp_cmd_ctrl ctrl;

    if (wb->need_rotate_hook)
        _exynosWbRegisterRotateHook(wb, TRUE);

    if (!_exynosWbCalTvoutRect(wb))
        goto fail_to_open;

    drmfmt = _exynosWbSupportFormat(wb->id);
    XDBG_GOTO_IF_FAIL(drmfmt > 0, fail_to_open);

    if ((wb->rotate) % 360 == 90)
        degree = EXYNOS_DRM_DEGREE_90;
    else if ((wb->rotate) % 360 == 180)
        degree = EXYNOS_DRM_DEGREE_180;
    else if ((wb->rotate) % 360 == 270)
        degree = EXYNOS_DRM_DEGREE_270;
    else
        degree = EXYNOS_DRM_DEGREE_0;

    CLEAR(property);
    property.config[0].ops_id = EXYNOS_DRM_OPS_SRC;
    property.config[0].fmt = DRM_FORMAT_YUV444;
    property.config[0].sz.hsize = (__u32) pExynosMode->main_lcd_mode.hdisplay;
    property.config[0].sz.vsize = (__u32) pExynosMode->main_lcd_mode.vdisplay;
    property.config[0].pos.x = 0;
    property.config[0].pos.y = 0;
    property.config[0].pos.w = (__u32) pExynosMode->main_lcd_mode.hdisplay;
    property.config[0].pos.h = (__u32) pExynosMode->main_lcd_mode.vdisplay;
    property.config[1].ops_id = EXYNOS_DRM_OPS_DST;
    property.config[1].degree = degree;
    property.config[1].fmt = drmfmt;
    property.config[1].sz.hsize = wb->width;
    property.config[1].sz.vsize = wb->height;
    property.config[1].pos.x = (__u32) wb->drm_dst.x;
    property.config[1].pos.y = (__u32) wb->drm_dst.y;
    property.config[1].pos.w = (__u32) wb->drm_dst.width;
    property.config[1].pos.h = (__u32) wb->drm_dst.height;
    property.cmd = IPP_CMD_WB;
#ifdef _F_WEARABLE_FEATURE_
    property.type = IPP_EVENT_DRIVEN;
#endif
    property.prop_id = wb->prop_id;
    property.refresh_rate = wb->hz;
#ifdef LEGACY_INTERFACE
    property.protect = wb->exynosure;
#endif

    wb->prop_id = exynosDrmIppSetProperty(wb->pScrn, &property);
    XDBG_GOTO_IF_FAIL(wb->prop_id >= 0, fail_to_open);

    XDBG_TRACE(MWB,
               "prop_id(%d) drmfmt(%c%c%c%c) size(%dx%d) crop(%d,%d %dx%d) rotate(%d)\n",
               wb->prop_id, FOURCC_STR(drmfmt), wb->width, wb->height,
               wb->drm_dst.x, wb->drm_dst.y, wb->drm_dst.width,
               wb->drm_dst.height, wb->rotate);

    if (!_exynosWbEnsureDrmDstBuffer(wb))
        goto fail_to_open;

    for (i = 0; i < wb->buf_num; i++) {
        struct drm_exynos_ipp_queue_buf buf;
        int j;

        if (wb->dst_buf[i]->showing) {
            XDBG_TRACE(MWB, "%d. name(%d) is showing\n", i,
                       wb->dst_buf[i]->keys[0]);
            continue;
        }

        CLEAR(buf);
        buf.ops_id = EXYNOS_DRM_OPS_DST;
        buf.buf_type = IPP_BUF_ENQUEUE;
        buf.prop_id = wb->prop_id;
        buf.buf_id = i;
        buf.user_data = (__u64) (uintptr_t) wb;

        XDBG_GOTO_IF_FAIL(wb->dst_buf[i] != NULL, fail_to_open);

        for (j = 0; j < PLANAR_CNT; j++)
            buf.handle[j] = wb->dst_buf[i]->handles[j];

        if (!exynosDrmIppQueueBuf(wb->pScrn, &buf))
            goto fail_to_open;

        wb->queued[i] = TRUE;
    }

    CLEAR(ctrl);
    ctrl.prop_id = wb->prop_id;
    ctrl.ctrl = IPP_CTRL_PLAY;
    if (!exynosDrmIppCmdCtrl(wb->pScrn, &ctrl))
        goto fail_to_open;

    wb->event_timer =
        TimerSet(wb->event_timer, 0, 3000, _exynosWbEventTimeout, wb);
    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;

 fail_to_open:

    _exynosWbCloseDrm(wb, FALSE);

    _exynosWbCloseDrmDstBuffer(wb);
    XDBG_DEBUG(MWB, "Q. ret FALSE\n");
    return FALSE;
}

static void
_exynosWbCloseDrm(EXYNOSWb * wb, Bool pause)
{
    XDBG_DEBUG(MWB, "E. wb %p pause %s\n", wb, pause ? "TRUE" : "FALSE");
    struct drm_exynos_ipp_cmd_ctrl ctrl;
    int i;

    _exynosWbCountStop(wb);

    XDBG_TRACE(MWB, "now_showing(%d) \n", wb->now_showing);

    /* pause : remain displaying layer buffer */
    if (wb->tv && !pause) {
        exynosVideoTvDisconnect(wb->tv);
        wb->tv = NULL;
        wb->wait_show = -1;
        wb->now_showing = -1;
    }

    for (i = 0; i < wb->buf_num; i++) {
        struct drm_exynos_ipp_queue_buf buf;
        int j;

        CLEAR(buf);
        buf.ops_id = EXYNOS_DRM_OPS_DST;
        buf.buf_type = IPP_BUF_DEQUEUE;
        buf.prop_id = wb->prop_id;
        buf.buf_id = i;

        if (wb->dst_buf[i])
            for (j = 0; j < EXYNOS_DRM_PLANAR_MAX; j++)
                buf.handle[j] = wb->dst_buf[i]->handles[j];

        exynosDrmIppQueueBuf(wb->pScrn, &buf);

        wb->queued[i] = FALSE;

    }

    CLEAR(ctrl);
    ctrl.prop_id = wb->prop_id;
    ctrl.ctrl = IPP_CTRL_STOP;
    exynosDrmIppCmdCtrl(wb->pScrn, &ctrl);

    wb->prop_id = -1;

    if (wb->rotate_timer) {
        TimerFree(wb->rotate_timer);
        wb->rotate_timer = NULL;
    }

    if (wb->event_timer) {
        TimerFree(wb->event_timer);
        wb->event_timer = NULL;
    }

    if (wb->ipp_timer) {
        TimerFree(wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    if (wb->need_rotate_hook)
        _exynosWbRegisterRotateHook(wb, FALSE);
    XDBG_DEBUG(MWB, "Q.\n");
}

static CARD32
_exynosWbRotateTimeout(OsTimerPtr timer, CARD32 now, pointer arg)
{
    XDBG_DEBUG(MWB, "E. timer %p now %" PRIXID " arg %p\n", timer, now, arg);
    EXYNOSWb *wb = (EXYNOSWb *) arg;
    PropertyPtr rotate_prop;

    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret 0\n");
        return 0;
    }
    if (wb->rotate_timer) {
        TimerFree(wb->rotate_timer);
        wb->rotate_timer = NULL;
    }

    rotate_prop =
        exynosUtilGetWindowProperty(wb->pScrn->pScreen->root,
                                    "_E_ILLUME_ROTATE_ROOT_ANGLE");
    if (rotate_prop) {
        int rotate = *(int *) rotate_prop->data;

        XDBG_TRACE(MWB, "timeout : rotate(%d)\n", rotate);

        if (wb->rotate != rotate)
            if (!exynosWbSetRotate(wb, rotate)) {
                XDBG_DEBUG(MWB, "Q. exynosWbSetRotate == FALSE. ret 0\n");
                return 0;
            }
    }

    /* make sure streaming is on. */
    exynosWbStart(wb);
    XDBG_DEBUG(MWB, "Q. ret 0\n");
    return 0;
}

static void
_exynosWbRotateHook(CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    XDBG_DEBUG(MWB, "E. pcbl %p unused %p calldata %p\n", pcbl, unused,
               calldata);
    EXYNOSWb *wb = (EXYNOSWb *) unused;
    ScrnInfoPtr pScrn;

    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }

    XacePropertyAccessRec *rec = (XacePropertyAccessRec *) calldata;
    PropertyPtr pProp = *rec->ppProp;
    Atom name = pProp->propertyName;

    pScrn = wb->pScrn;

    if (rec->pWin != pScrn->pScreen->root)      //Check Rootwindow
    {
        XDBG_DEBUG(MWB, "Q. rec->pWin(%p) != pScrn->pScreen->root(%p)\n",
                   rec->pWin, pScrn->pScreen->root);
        return;
    }
    if (name == atom_wb_rotate && (rec->access_mode & DixWriteAccess)) {
        int rotate = *(int *) pProp->data;
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;

        if (wb->rotate == rotate) {
            XDBG_DEBUG(MWB, "Q. wb->rotate(%d) == rotate(%d)\n",
                       wb->rotate, rotate);
            return;
        }
        XDBG_TRACE(MWB, "Change root angle(%d)\n", rotate);

        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            exynosWbStop(wb, FALSE);
        else
            exynosWbStop(wb, TRUE);

        wb->rotate_timer = TimerSet(wb->rotate_timer, 0, 800,
                                    _exynosWbRotateTimeout, wb);
    }
    XDBG_DEBUG(MWB, "Q.\n");
    return;
}

static Bool
_exynosWbRegisterRotateHook(EXYNOSWb * wb, Bool hook)
{
    XDBG_DEBUG(MWB, "E. wb %p hook %s\n", wb, hook ? "TRUE" : "FALSE");
    ScrnInfoPtr pScrn = wb->pScrn;

    XDBG_TRACE(MWB, "hook(%d) \n", hook);

    if (hook) {
        PropertyPtr rotate_prop;

        rotate_prop =
            exynosUtilGetWindowProperty(pScrn->pScreen->root,
                                        "_E_ILLUME_ROTATE_ROOT_ANGLE");
        if (rotate_prop) {
            int rotate = *(int *) rotate_prop->data;

            exynosWbSetRotate(wb, rotate);
        }

        /* Hook for window rotate */
        if (atom_wb_rotate == None)
            atom_wb_rotate = MakeAtom("_E_ILLUME_ROTATE_ROOT_ANGLE",
                                      strlen("_E_ILLUME_ROTATE_ROOT_ANGLE"),
                                      FALSE);

        if (atom_wb_rotate != None) {
            if (!XaceRegisterCallback
                (XACE_PROPERTY_ACCESS, _exynosWbRotateHook, wb))
                XDBG_ERROR(MWB, "fail to XaceRegisterCallback.\n");
        }
        else
            XDBG_WARNING(MWB, "Cannot find _E_ILLUME_ROTATE_ROOT_ANGLE\n");
    }
    else
        XaceDeleteCallback(XACE_PROPERTY_ACCESS, _exynosWbRotateHook, wb);
    XDBG_DEBUG(MWB, "Q.\n");
    return TRUE;
}

Bool
exynosWbIsOpened(void)
{
    XDBG_DEBUG(MWB, "E.\n");
    XDBG_DEBUG(MWB, "Q. ret = %s\n", keep_wb ? "TRUE" : "FALSE");
    return (keep_wb) ? TRUE : FALSE;
}

Bool
exynosWbIsRunning(void)
{
    XDBG_DEBUG(MWB, "E.\n");
    if (!keep_wb) {
        XDBG_DEBUG(MWB, "Q. keep_wb == NULL. ret FALSE\n");
        return FALSE;
    }
    XDBG_DEBUG(MWB, "Q. ret %s\n",
               (keep_wb->status == STATUS_RUNNING) ? "TRUE" : "FALSE");
    return (keep_wb->status == STATUS_RUNNING) ? TRUE : FALSE;
}

EXYNOSWb *
_exynosWbOpen(ScrnInfoPtr pScrn, unsigned int id, int width, int height,
              Bool scanout, int hz, Bool need_rotate_hook, const char *func)
{
    XDBG_DEBUG(MWB,
               "E. pScrn %p id %u width %d height %d scanout %s hz %d need_rotate_hook %s\n",
               pScrn, id, width, height, scanout ? "TRUE" : "FALSE", hz,
               need_rotate_hook ? "TRUE" : "FALSE");
    EXYNOSModePtr pExynosMode = NULL;

    if (!pScrn) {
        XDBG_DEBUG(MWB, "Q. pScrn == NULL. ret NULL\n");
        return NULL;
    }

    pExynosMode = EXYNOSPTR(pScrn)->pExynosMode;

    if (keep_wb) {
        XDBG_ERROR(MWB, "WB already opened. \n");
        XDBG_DEBUG(MWB, "Q. keep_wb(%p) != NULL. ret NULL\n", keep_wb);
        return NULL;
    }

    if (_exynosWbSupportFormat(id) == 0) {
        XDBG_ERROR(MWB, "'%c%c%c%c' not supported. \n", FOURCC_STR(id));
        XDBG_DEBUG(MWB, "Q. _exynosWbSupportFormat(%d) == 0. ret NULL\n", id);
        return NULL;
    }

    if (EXYNOSPTR(pScrn)->isLcdOff) {
        XDBG_ERROR(MWB, "Can't open wb during DPMS off. \n");
        XDBG_DEBUG(MWB, "Q. isLcdOff == TRUE. ret NULL\n");
        return NULL;
    }

    keep_wb = calloc(sizeof(EXYNOSWb), 1);

    if (!keep_wb) {
        XDBG_DEBUG(MWB, "Q. keep_wb == NULL. ret NULL\n");
        return NULL;
    }

    keep_wb->prop_id = -1;
    keep_wb->pScrn = pScrn;
    keep_wb->id = id;

    keep_wb->wait_show = -1;
    keep_wb->now_showing = -1;

    keep_wb->width = width;
    keep_wb->height = height;
    keep_wb->status = STATUS_STOP;

    keep_wb->scanout = scanout;
    keep_wb->hz = (hz > 0) ? hz : 60;

    if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_HDMI &&
        keep_wb->hz > pExynosMode->ext_connector_mode.vrefresh)
        keep_wb->buf_num = WB_BUF_MAX;
    else
        keep_wb->buf_num = WB_BUF_DEFAULT;

    if (id == FOURCC_RGB32 || id == FOURCC_RGB24)
        keep_wb->lpos = LAYER_DEFAULT;
    else
        keep_wb->lpos = LAYER_LOWER1;

    keep_wb->need_rotate_hook = need_rotate_hook;

    XDBG_SECURE(MWB,
                "wb(%p) id(%c%c%c%c) size(%dx%d) scanout(%d) hz(%d) rhoot(%d) buf_num(%d): %s\n",
                keep_wb, FOURCC_STR(id), keep_wb->width, keep_wb->height,
                scanout, hz, need_rotate_hook, keep_wb->buf_num, func);
    XDBG_DEBUG(MWB, "Q. ret %p(keep_wb)\n", keep_wb);
    return keep_wb;
}

void
_exynosWbClose(EXYNOSWb * wb, const char *func)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    EXYNOSWbNotifyFuncInfo *info, *tmp;

    if (!wb) {
        XDBG_ERROR(MWB, "wb is NULL\n");
        XDBG_DEBUG(MWB, "Q.\n");
        return;
    }
    if (keep_wb != wb) {
        XDBG_ERROR(MWB, "wb(%p) != keep_wb(%p)\n", wb, keep_wb);
        XDBG_DEBUG(MWB, "Q.\n");
        return;
    }
    exynosWbStop(wb, TRUE);

    XDBG_SECURE(MWB, "wb(%p): %s \n", wb, func);

    _exynosWbCallNotifyFunc(wb, WB_NOTI_CLOSED, NULL);

    nt_list_for_each_entry_safe(info, tmp, wb->info_list, next) {
        nt_list_del(info, wb->info_list, EXYNOSWbNotifyFuncInfo, next);
        free(info);
    }

    free(wb);
    keep_wb = NULL;
    XDBG_DEBUG(MWB, "Q.\n");
}

Bool
_exynosWbStart(EXYNOSWb * wb, const char *func)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    EXYNOSPtr pExynos;

    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL, ret FALSE\n");
        return FALSE;
    }
    pExynos = EXYNOSPTR(wb->pScrn);
    if (pExynos->isLcdOff) {
        XDBG_ERROR(MWB, "Can't start wb(%p) during DPMS off. \n", wb);
        XDBG_DEBUG(MWB, "Q. isLcdOff == TRUE. ret FALSE\n");
        return FALSE;
    }

    if (wb->status == STATUS_RUNNING) {
        XDBG_DEBUG(MWB, "Q. wb->status == STATUS_RUNNING. ret TRUE\n");
        return TRUE;
    }
    if (!_exynosWbOpenDrm(wb)) {
        XDBG_ERROR(MWB, "open fail. \n");
        XDBG_DEBUG(MWB, "Q. _exynosWbOpenDrm == FALSE. ret FALSE\n");
        return FALSE;
    }

    wb->status = STATUS_RUNNING;

    _exynosWbCallNotifyFunc(wb, WB_NOTI_START, NULL);

    XDBG_TRACE(MWB, "start: %s \n", func);

    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;
}

void
_exynosWbStop(EXYNOSWb * wb, Bool close_buf, const char *func)
{
    XDBG_DEBUG(MWB, "E. wb %p close_buf %s\n", wb,
               close_buf ? "TRUE" : "FALSE");
    if (!wb) {
        XDBG_ERROR(MWB, "wb is NULL\n");
        XDBG_DEBUG(MWB, "Q.\n");
        return;
    }
    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE)) {
        if (wb->rotate_timer) {
            TimerFree(wb->rotate_timer);
            wb->rotate_timer = NULL;
        }
        XDBG_DEBUG(MWB, "Q. wb->status == %s\n",
                   (wb->status ==
                    STATUS_STOP) ? "STATUS_STOP" : "STATUS_PAUSE");
        return;
    }

    _exynosWbCloseDrm(wb, FALSE);

    if (close_buf)
        _exynosWbCloseDrmDstBuffer(wb);
    else
        _exynosWbClearDrmDstBuffer(wb);

    wb->status = STATUS_STOP;

    _exynosWbCallNotifyFunc(wb, WB_NOTI_STOP, NULL);

    XDBG_TRACE(MWB, "stop: %s \n", func);
    XDBG_DEBUG(MWB, "Q.\n");
}

void
exynosWbPause(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }

    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE)) {
        if (wb->rotate_timer) {
            TimerFree(wb->rotate_timer);
            wb->rotate_timer = NULL;
        }
        XDBG_DEBUG(MWB, "Q. wb->status == %s\n",
                   (wb->status ==
                    STATUS_STOP) ? "STATUS_STOP" : "STATUS_PAUSE");
        return;
    }

    _exynosWbCloseDrm(wb, TRUE);

    _exynosWbCloseDrmDstBuffer(wb);

    wb->status = STATUS_PAUSE;

    _exynosWbCallNotifyFunc(wb, WB_NOTI_PAUSE, NULL);

    XDBG_TRACE(MWB, "pause: %s, wb(%p)\n", __func__, wb);
    XDBG_DEBUG(MWB, "Q.\n");
}

void
exynosWbResume(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }
    wb->wait_show = -1;
    wb->now_showing = -1;

    if (!exynosWbStart(wb)) {
        XDBG_ERROR(MWB, "wb(%p) start fail.%s\n", wb, __func__);
        exynosWbClose(wb);
        wb = NULL;
    }
    XDBG_TRACE(MWB, "start: %s, wb(%p)\n", __func__, wb);
    XDBG_DEBUG(MWB, "Q.\n");
}

Bool
exynosWbSetBuffer(EXYNOSWb * wb, EXYNOSVideoBuf ** vbufs, int bufnum)
{
    XDBG_DEBUG(MWB, "E. wb %p vbufs %p bufnum %d\n", wb, vbufs, bufnum);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret FALSE\n");
        return FALSE;
    }
    if (wb->status == STATUS_RUNNING) {
        XDBG_DEBUG(MWB, "Q. wb->status == STATUS_RUNNING. ret FALSE\n");
        return FALSE;
    }

    if (!vbufs) {
        XDBG_DEBUG(MWB, "Q. vbufs == NULL. ret FALSE\n");
        return FALSE;
    }

    if (wb->buf_num > bufnum) {
        XDBG_DEBUG(MWB, "Q. wb->buf_num(%d) > bufnum(%d). ret FALSE\n",
                   wb->buf_num, bufnum);
        return FALSE;
    }
    if (bufnum > WB_BUF_MAX) {
        XDBG_DEBUG(MWB, "Q. bufnum(%d) > WB_BUF_MAX(%d). ret FALSE\n", bufnum,
                   WB_BUF_MAX);
        return FALSE;
    }
    int i;

    for (i = 0; i < WB_BUF_MAX; i++) {
        if (wb->dst_buf[i]) {
            XDBG_ERROR(MWB, "already has %d buffers\n", wb->buf_num);
            XDBG_DEBUG(MWB, "Q. wb->dst_buf[%d] %p exist. ret FALSE\n", i,
                       wb->dst_buf[i]);
            return FALSE;
        }
    }

    for (i = 0; i < bufnum; i++) {
        XDBG_GOTO_IF_FAIL(wb->id == vbufs[i]->id, fail_set_buffer);
        XDBG_GOTO_IF_FAIL(wb->width == vbufs[i]->width, fail_set_buffer);
        XDBG_GOTO_IF_FAIL(wb->height == vbufs[i]->height, fail_set_buffer);
        XDBG_GOTO_IF_FAIL(wb->scanout == vbufs[i]->scanout, fail_set_buffer);

        wb->dst_buf[i] = exynosUtilVideoBufferRef(vbufs[i]);
        XDBG_GOTO_IF_FAIL(wb->dst_buf[i] != NULL, fail_set_buffer);

        if (!wb->dst_buf[i]->showing && wb->dst_buf[i]->need_reset)
            exynosUtilClearVideoBuffer(wb->dst_buf[i]);
        else
            wb->dst_buf[i]->need_reset = TRUE;
    }

    wb->buf_num = bufnum;
    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;

 fail_set_buffer:
    for (i = 0; i < WB_BUF_MAX; i++) {
        if (wb->dst_buf[i]) {
            exynosUtilVideoBufferUnref(wb->dst_buf[i]);
            wb->dst_buf[i] = NULL;
        }
    }
    XDBG_DEBUG(MWB, "Q. ret FALSE\n");
    return FALSE;
}

Bool
exynosWbSetRotate(EXYNOSWb * wb, int rotate)
{
    XDBG_DEBUG(MWB, "E. wb %p rotate %d\n", wb, rotate);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret FALSE\n");
        return FALSE;
    }
    if ((rotate % 90) != 0) {
        XDBG_DEBUG(MWB, "Q. rotate(%d) % 90 != 0. ret FALSE\n", rotate);
        return FALSE;
    }

    if (wb->rotate == rotate) {
        XDBG_DEBUG(MWB, "Q. wb->rotate(%d) == rotate(%d). ret TRUE\n",
                   wb->rotate, rotate);
        return TRUE;
    }

    XDBG_TRACE(MWB, "rotate(%d) \n", rotate);

    wb->rotate = rotate;

    if (wb->status == STATUS_RUNNING) {
        EXYNOSModePtr pExynosMode =
            (EXYNOSModePtr) EXYNOSPTR(wb->pScrn)->pExynosMode;

        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            exynosWbStop(wb, FALSE);
        else
            exynosWbStop(wb, TRUE);

        if (!exynosWbStart(wb)) {
            XDBG_DEBUG(MWB, "Q. exynosWbStart == FALSE. ret FALSE\n");
            return FALSE;
        }
    }
    XDBG_DEBUG(MWB, "Q. ret TRUE\n");
    return TRUE;
}

int
exynosWbGetRotate(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret -1\n");
        return -1;
    }
    XDBG_DEBUG(MWB, "Q. ret %d(wb->rotate)\n", wb->rotate);
    return wb->rotate;
}

void
exynosWbSetTvout(EXYNOSWb * wb, Bool enable)
{
    XDBG_DEBUG(MWB, "E. wb %p enable %s\n", wb, enable ? "TRUE" : "FALSE");
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }

    enable = (enable > 0) ? TRUE : FALSE;

    XDBG_TRACE(MWB, "tvout(%d) \n", enable);

    wb->tvout = enable;

    if (wb->status == STATUS_RUNNING) {
        exynosWbStop(wb, FALSE);

        if (!exynosWbStart(wb)) {
            XDBG_DEBUG(MWB, "Q. exynosWbStart == FALSE\n");
            return;
        }
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

Bool
exynosWbGetTvout(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret FALSE\n");
        return FALSE;
    }
    XDBG_DEBUG(MWB, "Q. ret %s(wb->tvout)\n", wb->tvout ? "TRUE" : "FALSE");
    return wb->tvout;
}

void
exynosWbSetSecure(EXYNOSWb * wb, Bool exynosure)
{
    XDBG_DEBUG(MWB, "E. wb %p exynosure %s\n", wb,
               exynosure ? "TRUE" : "FALSE");
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }

    exynosure = (exynosure > 0) ? TRUE : FALSE;

    if (wb->exynosure == exynosure) {
        XDBG_DEBUG(MWB, "Q. wb->exynosure(%s) == exynosure(%s)\n",
                   wb->exynosure ? "TRUE" : "FALSE",
                   exynosure ? "TRUE" : "FALSE");
        return;
    }
    XDBG_TRACE(MWB, "exynosure(%d) \n", exynosure);

    wb->exynosure = exynosure;

    if (wb->status == STATUS_RUNNING) {
        exynosWbStop(wb, TRUE);

        if (!exynosWbStart(wb)) {
            XDBG_DEBUG(MWB, "Q. exynosWbStart == FALSE\n");
            return;
        }
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

Bool
exynosWbGetSecure(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p\n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret FALSE\n");
        return FALSE;
    }
    XDBG_DEBUG(MWB, "Q. ret %s(wb->exynosure)\n",
               wb->exynosure ? "TRUE" : "FALSE");
    return wb->exynosure;
}

void
exynosWbGetSize(EXYNOSWb * wb, int *width, int *height)
{
    XDBG_DEBUG(MWB, "E. wb %p width %p height %p\n", wb, width, height);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL.\n");
        return;
    }

    if (width)
        *width = wb->width;

    if (height)
        *height = wb->height;
    XDBG_DEBUG(MWB, "Q. width %d height %d\n", wb->width, wb->height);
}

Bool
exynosWbCanDequeueBuffer(EXYNOSWb * wb)
{
    XDBG_DEBUG(MWB, "E. wb %p \n", wb);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL. ret FALSE\n");
        return FALSE;
    }
    int i, remain = 0;

    for (i = 0; i < wb->buf_num; i++)
        if (wb->queued[i])
            remain++;

    XDBG_TRACE(MWB, "buf_num(%d) remain(%d)\n", wb->buf_num, remain);
    XDBG_DEBUG(MWB, "Q. ret %s\n", (remain > WB_BUF_MIN) ? "TRUE" : "FALSE");
    return (remain > WB_BUF_MIN) ? TRUE : FALSE;
}

void
exynosWbQueueBuffer(EXYNOSWb * wb, EXYNOSVideoBuf * vbuf)
{
    XDBG_DEBUG(MWB, "E. wb %p vbuf %p\n", wb, vbuf);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }
    if (!vbuf) {
        XDBG_DEBUG(MWB, "Q. vbuf == NULL\n");
        return;
    }
    if (vbuf->showing == TRUE) {
        XDBG_DEBUG(MWB, "Q. vbuf->showing == TRUE\n");
        return;
    }
    int i;

    if (wb->prop_id == -1) {
        XDBG_DEBUG(MWB, "Q. wb->prop_id == -1\n");
        return;
    }
    for (i = 0; i < wb->buf_num; i++)
        if (wb->dst_buf[i] == vbuf) {
            XDBG_TRACE(MWB, "%d queueing.\n", i);
            _exynosWbQueue(wb, i);
        }
    XDBG_DEBUG(MWB, "Q.\n");
}

void
exynosWbAddNotifyFunc(EXYNOSWb * wb, EXYNOSWbNotify noti, WbNotifyFunc func,
                      void *user_data)
{
    XDBG_DEBUG(MWB, "E. wb %p noti %d func %p user_data %p\n", wb, noti, func,
               user_data);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }
    if (!func) {
        XDBG_DEBUG(MWB, "Q. func == NULL\n");
        return;
    }
    EXYNOSWbNotifyFuncInfo *info;

    nt_list_for_each_entry(info, wb->info_list, next) {
        if (info->func == func) {
            XDBG_DEBUG(MWB, "Q. info->func(%p) == func(%p)\n", info->func,
                       func);
            return;
        }
    }

    XDBG_TRACE(MWB, "noti(%d) func(%p) user_data(%p) \n", noti, func,
               user_data);

    info = calloc(1, sizeof(EXYNOSWbNotifyFuncInfo));
    if (!info) {
        XDBG_DEBUG(MWB, "Q. info == NULL\n");
        return;
    }

    info->noti = noti;
    info->func = func;
    info->user_data = user_data;

    if (wb->info_list)
        nt_list_append(info, wb->info_list, EXYNOSWbNotifyFuncInfo, next);
    else
        wb->info_list = info;
    XDBG_DEBUG(MWB, "Q.\n");
}

void
exynosWbRemoveNotifyFunc(EXYNOSWb * wb, WbNotifyFunc func)
{
    XDBG_DEBUG(MWB, "E. wb %p func %p\n", wb, func);
    if (!wb) {
        XDBG_DEBUG(MWB, "Q. wb == NULL\n");
        return;
    }
    if (!func) {
        XDBG_DEBUG(MWB, "Q. func == NULL\n");
        return;
    }

    EXYNOSWbNotifyFuncInfo *info;

    nt_list_for_each_entry(info, wb->info_list, next) {
        if (info->func == func) {
            XDBG_DEBUG(MWB, "func(%p) \n", func);
            nt_list_del(info, wb->info_list, EXYNOSWbNotifyFuncInfo, next);
            {
                XDBG_DEBUG(MWB, "Q. info->func(%p) == func(%p)\n", info->func,
                           func);
            }
            free(info);
            return;
        }
    }
    XDBG_DEBUG(MWB, "Q.\n");
}

EXYNOSWb *
exynosWbGet(void)
{
    XDBG_DEBUG(MWB, "E.\n");
    XDBG_DEBUG(MWB, "Q. ret %p(keep_wb)\n", keep_wb);
    return keep_wb;
}

void
exynosWbDestroy(void)
{
    XDBG_DEBUG(MWB, "E.\n");
    if (!keep_wb) {
        XDBG_DEBUG(MWB, "Q. keep_wb == NULL\n");
        return;
    }
    exynosWbClose(keep_wb);
    XDBG_DEBUG(MWB, "Q.\n");
}
