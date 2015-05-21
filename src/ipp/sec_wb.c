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

#include "sec.h"
#include "sec_util.h"
#include "sec_crtc.h"
#include "sec_video_fourcc.h"
#include "sec_video_tvout.h"
#include "sec_wb.h"
#include "sec_drm_ipp.h"
#include "sec_converter.h"

#include <drm_fourcc.h>

#define WB_BUF_MAX      5
#define WB_BUF_DEFAULT  3
#define WB_BUF_MIN      2

enum
{
    PENDING_NONE,
    PENDING_ROTATE,
    PENDING_STOP,
    PENDING_CLOSE,
};

enum
{
    STATUS_RUNNING,
    STATUS_PAUSE,
    STATUS_STOP,
};

typedef struct _SECWbNotifyFuncInfo
{
    SECWbNotify   noti;
    WbNotifyFunc  func;
    void         *user_data;

    struct _SECWbNotifyFuncInfo *next;
} SECWbNotifyFuncInfo;

struct _SECWb
{
    int  prop_id;

    ScrnInfoPtr pScrn;

    unsigned int id;

    int     rotate;

    int     width;
    int     height;
    xRectangle  drm_dst;
    xRectangle  tv_dst;

    SECVideoBuf *dst_buf[WB_BUF_MAX];
    Bool         queued[WB_BUF_MAX];
    int          buf_num;

    int     wait_show;
    int     now_showing;

    SECWbNotifyFuncInfo *info_list;

    /* for tvout */
    Bool        tvout;
    SECVideoTv *tv;
    SECLayerPos lpos;

    Bool       need_rotate_hook;
    OsTimerPtr rotate_timer;

    /* count */
    unsigned int put_counts;
    unsigned int last_counts;
    OsTimerPtr timer;

    OsTimerPtr event_timer;
    OsTimerPtr ipp_timer;

    Bool scanout;
    int  hz;

    int     status;
    Bool    secure;
    CARD32  prev_time;
};

static unsigned int formats[] =
{
    FOURCC_RGB32,
    FOURCC_ST12,
    FOURCC_SN12,
};

static SECWb *keep_wb;
static Atom atom_wb_rotate;

static void _secWbQueue (SECWb *wb, int index);
static Bool _secWbRegisterRotateHook (SECWb *wb, Bool hook);
static void _secWbCloseDrmDstBuffer (SECWb *wb);
static void _secWbCloseDrm (SECWb *wb, Bool pause);

static CARD32
_secWbCountPrint (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECWb *wb = (SECWb*)arg;

    ErrorF ("IppEvent : %d fps. \n", wb->put_counts - wb->last_counts);

    wb->last_counts = wb->put_counts;

    wb->timer = TimerSet (wb->timer, 0, 1000, _secWbCountPrint, arg);

    return 0;
}

static void
_secWbCountFps (SECWb *wb)
{
    wb->put_counts++;

    if (wb->timer)
        return;

    wb->timer = TimerSet (NULL, 0, 1000, _secWbCountPrint, wb);
}

static void
_secWbCountStop (SECWb *wb)
{
    if (wb->timer)
    {
        TimerFree (wb->timer);
        wb->timer = NULL;
    }

    wb->put_counts = 0;
    wb->last_counts = 0;
}

static unsigned int
_secWbSupportFormat (int id)
{
    unsigned int *drmfmts;
    int i, size, num = 0;
    unsigned int drmfmt = secUtilGetDrmFormat (id);

    size = sizeof (formats) / sizeof (unsigned int);

    for (i = 0; i < size; i++)
        if (formats[i] == id)
            break;

    if (i == size)
    {
        XDBG_ERROR (MWB, "wb not support : '%c%c%c%c'.\n", FOURCC_STR (id));
        return 0;
    }

    drmfmts = secDrmIppGetFormatList (&num);
    if (!drmfmts)
    {
        XDBG_ERROR (MWB, "no drm format list.\n");
        return 0;
    }

    for (i = 0; i < num; i++)
        if (drmfmts[i] == drmfmt)
        {
            free (drmfmts);
            return drmfmt;
        }

    XDBG_ERROR (MWB, "drm ipp not support : '%c%c%c%c'.\n", FOURCC_STR (id));

    free (drmfmts);

    return 0;
}

static void
_secWbCallNotifyFunc (SECWb *wb, SECWbNotify noti, void *noti_data)
{
    SECWbNotifyFuncInfo *info;

    nt_list_for_each_entry (info, wb->info_list, next)
    {
        if (info->noti == noti && info->func)
            info->func (wb, noti, noti_data, info->user_data);
    }
}

static void
_secWbLayerNotifyFunc (SECLayer *layer, int type, void *type_data, void *data)
{
    SECWb *wb = (SECWb*)data;
    SECVideoBuf *vbuf = (SECVideoBuf*)type_data;

    if (type != LAYER_VBLANK)
        return;

    XDBG_RETURN_IF_FAIL (wb != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (vbuf));

    if (wb->status == STATUS_PAUSE) {
        XDBG_WARNING (MWB, "pause status. return.\n");
        return;
    }

    if (wb->wait_show >= 0 && wb->dst_buf[wb->wait_show] != vbuf)
        XDBG_WARNING (MWB, "wait_show(%d,%p) != showing_vbuf(%p). \n",
                      wb->wait_show, wb->dst_buf[wb->wait_show], vbuf);

    if (wb->now_showing >= 0)
        _secWbQueue (wb, wb->now_showing);

    wb->now_showing = wb->wait_show;
    wb->wait_show = -1;

    XDBG_DEBUG (MWB, "now_showing(%d,%p). \n", wb->now_showing, vbuf);
}

static Bool
_secWbCalTvoutRect (SECWb *wb)
{
    SECModePtr pSecMode = (SECModePtr) SECPTR (wb->pScrn)->pSecMode;
    int src_w, src_h, dst_w, dst_h;

    if (!wb->tvout)
    {
        if (wb->width == 0 || wb->height == 0)
        {
            wb->width = pSecMode->main_lcd_mode.hdisplay;
            wb->height = pSecMode->main_lcd_mode.vdisplay;
        }

        src_w = pSecMode->main_lcd_mode.hdisplay;
        src_h = pSecMode->main_lcd_mode.vdisplay;
        dst_w = wb->width;
        dst_h = wb->height;
        XDBG_RETURN_VAL_IF_FAIL (src_w > 0 && src_h > 0, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (dst_w > 0 && dst_h > 0, FALSE);

        if (wb->rotate % 180)
            SWAP (src_w, src_h);

        secUtilAlignRect (src_w, src_h, dst_w, dst_h, &wb->drm_dst, TRUE);
    }
    else
    {
        src_w = (int)pSecMode->main_lcd_mode.hdisplay;
        src_h = (int)pSecMode->main_lcd_mode.vdisplay;
        dst_w = (int)pSecMode->ext_connector_mode.hdisplay;
        dst_h = (int)pSecMode->ext_connector_mode.vdisplay;
        XDBG_RETURN_VAL_IF_FAIL (src_w > 0 && src_h > 0, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (dst_w > 0 && dst_h > 0, FALSE);

        if (wb->rotate % 180)
            SWAP (src_w, src_h);

        if (wb->lpos == LAYER_UPPER)
        {
            /* Mixer can't scale. */
            wb->width = dst_w;
            wb->height = dst_h;
            wb->tv_dst.width = wb->width;
            wb->tv_dst.height = wb->height;

            secUtilAlignRect (src_w, src_h, dst_w, dst_h, &wb->drm_dst, TRUE);
        }
        else /* LAYER_LOWER1 */
        {
            /* VP can scale */
            secUtilAlignRect (src_w, src_h, dst_w, dst_h, &wb->tv_dst, TRUE);

            wb->width = src_w;
            wb->height = src_h;

            wb->drm_dst.width = wb->width;
            wb->drm_dst.height = wb->height;
        }
    }

    XDBG_TRACE (MWB, "tvout(%d) lpos(%d) src(%dx%d) drm_dst(%d,%d %dx%d) tv_dst(%d,%d %dx%d).\n",
                wb->tvout, wb->lpos, wb->width, wb->height,
                wb->drm_dst.x, wb->drm_dst.y, wb->drm_dst.width, wb->drm_dst.height,
                wb->tv_dst.x, wb->tv_dst.y, wb->tv_dst.width, wb->tv_dst.height);

    return TRUE;
}

static void
_secWbQueue (SECWb *wb, int index)
{
    struct drm_exynos_ipp_queue_buf buf;
    int j;

    if (index < 0)
        return;

    XDBG_RETURN_IF_FAIL (wb->dst_buf[index]->showing == FALSE);

    CLEAR (buf);
    buf.ops_id = EXYNOS_DRM_OPS_DST;
    buf.buf_type = IPP_BUF_ENQUEUE;
    buf.prop_id = wb->prop_id;
    buf.buf_id = index;
    buf.user_data = (__u64)(uintptr_t)wb;

    for (j = 0; j < PLANAR_CNT; j++)
        buf.handle[j] = wb->dst_buf[index]->handles[j];

    if (!secDrmIppQueueBuf (wb->pScrn, &buf))
        return;

    wb->queued[index] = TRUE;
    wb->dst_buf[index]->dirty = TRUE;

    XDBG_DEBUG (MWB, "index(%d)\n", index);
}

static int
_secWbDequeued (SECWb *wb, Bool skip_put, int index)
{
    int i, remain = 0;

    XDBG_RETURN_VAL_IF_FAIL (index < wb->buf_num, -1);
    XDBG_WARNING_IF_FAIL (wb->dst_buf[index]->showing == FALSE);

    XDBG_DEBUG (MWB, "skip_put(%d) index(%d)\n", skip_put, index);

    if (!wb->queued[index])
        XDBG_WARNING (MWB, "buf(%d) already dequeued.\n", index);

    wb->queued[index] = FALSE;

    for (i = 0; i< wb->buf_num; i++)
    {
        if (wb->queued[i])
            remain++;
    }

    /* the count of remain buffers should be more than 2. */
    if (remain >= WB_BUF_MIN)
        _secWbCallNotifyFunc (wb, WB_NOTI_IPP_EVENT, (void*)wb->dst_buf[index]);
    else
        XDBG_DEBUG (MWB, "remain buffer count: %d\n", remain);

    if (wb->tvout)
    {
        if (!wb->tv)
        {
            if (wb->tv_dst.width > 0 && wb->tv_dst.height > 0)
                wb->tv = secVideoTvConnect (wb->pScrn, wb->id, wb->lpos);

            if (wb->tv && !secUtilEnsureExternalCrtc (wb->pScrn))
            {
                wb->tvout = FALSE;
                secVideoTvDisconnect (wb->tv);
                wb->tv = NULL;
            }

            if (wb->tv)
            {
                SECLayer *layer = secVideoTvGetLayer (wb->tv);
                secLayerEnableVBlank (layer, TRUE);
                secLayerAddNotifyFunc (layer, _secWbLayerNotifyFunc, wb);
            }
        }

        if (!skip_put && wb->tv)
        {
            wb->wait_show = index;
            secVideoTvPutImage (wb->tv, wb->dst_buf[index], &wb->tv_dst, 0);
        }
    }

    if (remain == 0)
        XDBG_ERROR (MWB, "*** wb's buffer empty!! *** \n");

    XDBG_DEBUG (MWB, "tv(%p) wait_show(%d) remain(%d)\n", wb->tv,
                wb->wait_show, remain);

    return index;
}

static CARD32
_secWbIppRetireTimeout (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECWb *wb = (SECWb*)arg;

    if (wb->ipp_timer)
    {
        TimerFree (wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    XDBG_ERROR (MWB, "failed : +++ WB IPP Retire Timeout!!\n");

    return 0;
}

unsigned int
secWbGetPropID (void)
{
    if (!keep_wb)
        return -1;

    return keep_wb->prop_id;
}

void
secWbHandleIppEvent (int fd, unsigned int *buf_idx, void *data)
{
    SECWb *wb = (SECWb*)data;
    SECPtr pSec;
    int index = buf_idx[EXYNOS_DRM_OPS_DST];

    if (!wb)
    {
        XDBG_ERROR (MWB, "data is %p \n", data);
        return;
    }

    if (keep_wb != wb)
    {
        XDBG_WARNING (MWB, "Useless ipp event! (%p) \n", wb);
        return;
    }

    if (wb->event_timer)
    {
        TimerFree (wb->event_timer);
        wb->event_timer = NULL;
    }

    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE))
    {
        XDBG_ERROR (MWB, "stop or pause. ignore a event. %p, (status:%d)\n",
            data, wb->status);
        return;
    }

    if (wb->ipp_timer)
    {
        TimerFree (wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    wb->ipp_timer = TimerSet (wb->ipp_timer, 0, 2000, _secWbIppRetireTimeout, wb);

    XDBG_DEBUG (MWB, "=============== wb(%p) !\n", wb);

    pSec = SECPTR (wb->pScrn);

    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_WB)
    {
        CARD32 cur, sub;
        cur = GetTimeInMillis ();
        sub = cur - wb->prev_time;
        wb->prev_time = cur;
        ErrorF ("wb evt interval  : %6"PRIXID" ms\n", sub);
    }

    if (pSec->wb_fps)
        _secWbCountFps (wb);
    else
        _secWbCountStop (wb);

    if (wb->rotate_timer)
    {
        _secWbDequeued (wb, TRUE, index);
        _secWbQueue (wb, index);
    }
    else
    {
        if (wb->wait_show >= 0)
        {
            _secWbDequeued (wb, TRUE, index);
            _secWbQueue (wb, index);
        }
        else
        {
            _secWbDequeued (wb, FALSE, index);

            if (wb->wait_show < 0 && !wb->dst_buf[index]->showing)
                _secWbQueue (wb, index);
        }
    }

    _secWbCallNotifyFunc (wb, WB_NOTI_IPP_EVENT_DONE, NULL);

    XDBG_DEBUG (MWB, "=============== !\n");
}

static Bool
_secWbEnsureDrmDstBuffer (SECWb *wb)
{
    int i;

    for (i = 0; i < wb->buf_num; i++)
    {
        if (wb->dst_buf[i])
        {
            secUtilClearVideoBuffer (wb->dst_buf[i]);
            continue;
        }

        wb->dst_buf[i] = secUtilAllocVideoBuffer (wb->pScrn, wb->id,
                                                  wb->width, wb->height,
                                                  keep_wb->scanout, TRUE,
                                                  secVideoIsSecureMode (wb->pScrn));
        XDBG_GOTO_IF_FAIL (wb->dst_buf[i] != NULL, fail_to_ensure);
    }

    return TRUE;
fail_to_ensure:
    _secWbCloseDrmDstBuffer (wb);

    return FALSE;
}

static void
_secWbCloseDrmDstBuffer (SECWb *wb)
{
    int i;

    for (i = 0; i < wb->buf_num; i++)
    {
        if (wb->dst_buf[i])
        {
            secUtilVideoBufferUnref (wb->dst_buf[i]);
            wb->dst_buf[i] = NULL;
        }
    }
}

static void
_secWbClearDrmDstBuffer (SECWb *wb)
{
    int i;

    for (i = 0; i < wb->buf_num; i++)
    {
        if (wb->dst_buf[i])
        {
            if (!wb->dst_buf[i]->showing && wb->dst_buf[i]->need_reset)
                secUtilClearVideoBuffer (wb->dst_buf[i]);
            else
                wb->dst_buf[i]->need_reset = TRUE;
        }
    }
}

static CARD32
_secWbEventTimeout (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECWb *wb = (SECWb*) arg;

    if (!wb)
        return 0;

    if (wb->event_timer)
    {
        TimerFree (wb->event_timer);
        wb->event_timer = NULL;
    }

    XDBG_ERROR (MWB, "*** ipp event not happen!! \n");

    return 0;
}

static Bool
_secWbOpenDrm (SECWb *wb)
{
    SECPtr pSec = SECPTR (wb->pScrn);
    SECModePtr pSecMode = (SECModePtr)pSec->pSecMode;
    int i;
    unsigned int drmfmt = 0;
    struct drm_exynos_ipp_property property;
    enum drm_exynos_degree degree;
    struct drm_exynos_ipp_cmd_ctrl ctrl;

    if (wb->need_rotate_hook)
        _secWbRegisterRotateHook (wb, TRUE);

    if (!_secWbCalTvoutRect (wb))
        goto fail_to_open;

    drmfmt = _secWbSupportFormat (wb->id);
    XDBG_GOTO_IF_FAIL (drmfmt > 0, fail_to_open);

    if ((wb->rotate) % 360 == 90)
        degree = EXYNOS_DRM_DEGREE_90;
    else if ((wb->rotate) % 360 == 180)
        degree = EXYNOS_DRM_DEGREE_180;
    else if ((wb->rotate) % 360 == 270)
        degree = EXYNOS_DRM_DEGREE_270;
    else
        degree = EXYNOS_DRM_DEGREE_0;

    CLEAR (property);
    property.config[0].ops_id = EXYNOS_DRM_OPS_SRC;
    property.config[0].fmt = DRM_FORMAT_YUV444;
    property.config[0].sz.hsize = (__u32)pSecMode->main_lcd_mode.hdisplay;
    property.config[0].sz.vsize = (__u32)pSecMode->main_lcd_mode.vdisplay;
    property.config[0].pos.x = 0;
    property.config[0].pos.y = 0;
    property.config[0].pos.w = (__u32)pSecMode->main_lcd_mode.hdisplay;
    property.config[0].pos.h = (__u32)pSecMode->main_lcd_mode.vdisplay;
    property.config[1].ops_id = EXYNOS_DRM_OPS_DST;
    property.config[1].degree = degree;
    property.config[1].fmt = drmfmt;
    property.config[1].sz.hsize = wb->width;
    property.config[1].sz.vsize = wb->height;
    property.config[1].pos.x = (__u32)wb->drm_dst.x;
    property.config[1].pos.y = (__u32)wb->drm_dst.y;
    property.config[1].pos.w = (__u32)wb->drm_dst.width;
    property.config[1].pos.h = (__u32)wb->drm_dst.height;
    property.cmd = IPP_CMD_WB;
#ifdef _F_WEARABLE_FEATURE_
    property.type = IPP_EVENT_DRIVEN;
#endif
    property.prop_id = wb->prop_id;
    property.refresh_rate = wb->hz;
#ifdef LEGACY_INTERFACE
    property.protect = wb->secure;
#endif

    wb->prop_id = secDrmIppSetProperty (wb->pScrn, &property);
    XDBG_GOTO_IF_FAIL (wb->prop_id >= 0, fail_to_open);

    XDBG_TRACE (MWB, "prop_id(%d) drmfmt(%c%c%c%c) size(%dx%d) crop(%d,%d %dx%d) rotate(%d)\n",
                wb->prop_id, FOURCC_STR(drmfmt), wb->width, wb->height,
                wb->drm_dst.x, wb->drm_dst.y, wb->drm_dst.width, wb->drm_dst.height,
                wb->rotate);

    if (!_secWbEnsureDrmDstBuffer (wb))
        goto fail_to_open;

    for (i = 0; i < wb->buf_num; i++)
    {
        struct drm_exynos_ipp_queue_buf buf;
        int j;

        if (wb->dst_buf[i]->showing)
        {
            XDBG_DEBUG (MWB, "%d. name(%d) is showing\n", i, wb->dst_buf[i]->keys[0]);
            continue;
        }

        CLEAR (buf);
        buf.ops_id = EXYNOS_DRM_OPS_DST;
        buf.buf_type = IPP_BUF_ENQUEUE;
        buf.prop_id = wb->prop_id;
        buf.buf_id = i;
        buf.user_data = (__u64)(uintptr_t)wb;

        XDBG_GOTO_IF_FAIL (wb->dst_buf[i] != NULL, fail_to_open);

        for (j = 0; j < PLANAR_CNT; j++)
            buf.handle[j] = wb->dst_buf[i]->handles[j];

        if (!secDrmIppQueueBuf (wb->pScrn, &buf))
            goto fail_to_open;

        wb->queued[i] = TRUE;
    }

    CLEAR (ctrl);
    ctrl.prop_id = wb->prop_id;
    ctrl.ctrl = IPP_CTRL_PLAY;
    if (!secDrmIppCmdCtrl (wb->pScrn, &ctrl))
        goto fail_to_open;

    wb->event_timer = TimerSet (wb->event_timer, 0, 3000, _secWbEventTimeout, wb);

    return TRUE;

fail_to_open:

    _secWbCloseDrm (wb, FALSE);

    _secWbCloseDrmDstBuffer (wb);

    return FALSE;
}

static void
_secWbCloseDrm (SECWb *wb, Bool pause)
{
    struct drm_exynos_ipp_cmd_ctrl ctrl;
    int i;

    _secWbCountStop (wb);

    XDBG_TRACE (MWB, "now_showing(%d) \n", wb->now_showing);

    /* pause : remain displaying layer buffer */
    if (wb->tv && !pause)
    {
        secVideoTvDisconnect (wb->tv);
        wb->tv = NULL;
        wb->wait_show = -1;
        wb->now_showing = -1;
    }

    for (i = 0; i < wb->buf_num; i++)
    {
        struct drm_exynos_ipp_queue_buf buf;
        int j;

        CLEAR (buf);
        buf.ops_id = EXYNOS_DRM_OPS_DST;
        buf.buf_type = IPP_BUF_DEQUEUE;
        buf.prop_id = wb->prop_id;
        buf.buf_id = i;

        if (wb->dst_buf[i])
            for (j = 0; j < EXYNOS_DRM_PLANAR_MAX; j++)
                buf.handle[j] = wb->dst_buf[i]->handles[j];

       secDrmIppQueueBuf (wb->pScrn, &buf);

       wb->queued[i] = FALSE;

    }

    CLEAR (ctrl);
    ctrl.prop_id = wb->prop_id;
    ctrl.ctrl = IPP_CTRL_STOP;
    secDrmIppCmdCtrl (wb->pScrn, &ctrl);

    wb->prop_id = -1;

    if (wb->rotate_timer)
    {
        TimerFree (wb->rotate_timer);
        wb->rotate_timer = NULL;
    }

    if (wb->event_timer)
    {
        TimerFree (wb->event_timer);
        wb->event_timer = NULL;
    }

    if (wb->ipp_timer)
    {
        TimerFree (wb->ipp_timer);
        wb->ipp_timer = NULL;
    }

    if (wb->need_rotate_hook)
        _secWbRegisterRotateHook (wb, FALSE);
}

static CARD32
_secWbRotateTimeout (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECWb *wb = (SECWb*) arg;
    PropertyPtr rotate_prop;

    if (!wb)
        return 0;

    if (wb->rotate_timer)
    {
        TimerFree (wb->rotate_timer);
        wb->rotate_timer = NULL;
    }

    rotate_prop = secUtilGetWindowProperty (wb->pScrn->pScreen->root, "_E_ILLUME_ROTATE_ROOT_ANGLE");
    if (rotate_prop)
    {
        int rotate = *(int*)rotate_prop->data;

        XDBG_TRACE (MWB, "timeout : rotate(%d)\n", rotate);

        if (wb->rotate != rotate)
            if (!secWbSetRotate (wb, rotate))
                return 0;
    }

    /* make sure streaming is on. */
    secWbStart (wb);

    return 0;
}

static void
_secWbRotateHook (CallbackListPtr *pcbl, pointer unused, pointer calldata)
{
    SECWb *wb = (SECWb*) unused;
    ScrnInfoPtr pScrn;

    XDBG_RETURN_IF_FAIL (wb != NULL);

    XacePropertyAccessRec *rec = (XacePropertyAccessRec*)calldata;
    PropertyPtr pProp = *rec->ppProp;
    Atom name = pProp->propertyName;

    pScrn = wb->pScrn;

    if (rec->pWin != pScrn->pScreen->root)  //Check Rootwindow
        return;

    if (name == atom_wb_rotate && (rec->access_mode & DixWriteAccess))
    {
        int rotate = *(int*)pProp->data;
        SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;

        if (wb->rotate == rotate)
            return;

        XDBG_TRACE (MWB, "Change root angle(%d)\n", rotate);

        if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            secWbStop (wb, FALSE);
        else
            secWbStop (wb, TRUE);

        wb->rotate_timer = TimerSet (wb->rotate_timer, 0, 800,
                                     _secWbRotateTimeout,
                                     wb);
    }

    return;
}

static Bool
_secWbRegisterRotateHook (SECWb *wb, Bool hook)
{
    ScrnInfoPtr pScrn = wb->pScrn;

    XDBG_TRACE (MWB, "hook(%d) \n", hook);

    if (hook)
    {
        PropertyPtr rotate_prop;

        rotate_prop = secUtilGetWindowProperty (pScrn->pScreen->root, "_E_ILLUME_ROTATE_ROOT_ANGLE");
        if (rotate_prop)
        {
            int rotate = *(int*)rotate_prop->data;
            secWbSetRotate (wb, rotate);
        }

        /* Hook for window rotate */
        if (atom_wb_rotate == None)
            atom_wb_rotate = MakeAtom ("_E_ILLUME_ROTATE_ROOT_ANGLE",
                                       strlen ("_E_ILLUME_ROTATE_ROOT_ANGLE"), FALSE);

        if (atom_wb_rotate != None)
        {
            if (!XaceRegisterCallback (XACE_PROPERTY_ACCESS, _secWbRotateHook, wb))
                XDBG_ERROR (MWB, "fail to XaceRegisterCallback.\n");
        }
        else
            XDBG_WARNING (MWB, "Cannot find _E_ILLUME_ROTATE_ROOT_ANGLE\n");
    }
    else
        XaceDeleteCallback (XACE_PROPERTY_ACCESS, _secWbRotateHook, wb);

    return TRUE;
}

Bool
secWbIsOpened (void)
{
    return (keep_wb) ? TRUE : FALSE;
}

Bool
secWbIsRunning (void)
{
    if (!keep_wb)
        return FALSE;

    return (keep_wb->status == STATUS_RUNNING) ? TRUE : FALSE;
}

SECWb*
_secWbOpen (ScrnInfoPtr pScrn, unsigned int id, int width, int height,
            Bool scanout, int hz, Bool need_rotate_hook, const char *func)
{
    SECModePtr pSecMode = SECPTR (pScrn)->pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (pScrn != NULL, NULL);

    if (keep_wb)
    {
        XDBG_ERROR (MWB, "WB already opened. \n");
        return NULL;
    }

    if (_secWbSupportFormat (id) == 0)
    {
        XDBG_ERROR (MWB, "'%c%c%c%c' not supported. \n", FOURCC_STR (id));
        return NULL;
    }

    if (SECPTR (pScrn)->isLcdOff)
    {
        XDBG_ERROR (MWB, "Can't open wb during DPMS off. \n");
        return NULL;
    }

    if (!keep_wb)
        keep_wb = calloc (sizeof (SECWb), 1);

    XDBG_RETURN_VAL_IF_FAIL (keep_wb != NULL, FALSE);

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

    if (pSecMode->conn_mode == DISPLAY_CONN_MODE_HDMI &&
        keep_wb->hz > pSecMode->ext_connector_mode.vrefresh)
        keep_wb->buf_num = WB_BUF_MAX;
    else
        keep_wb->buf_num = WB_BUF_DEFAULT;

    if (id == FOURCC_RGB32)
        keep_wb->lpos = LAYER_UPPER;
    else
        keep_wb->lpos = LAYER_LOWER1;

    keep_wb->need_rotate_hook = need_rotate_hook;

    XDBG_SECURE (MWB, "wb(%p) id(%c%c%c%c) size(%dx%d) scanout(%d) hz(%d) rhoot(%d) buf_num(%d): %s\n", keep_wb,
                 FOURCC_STR(id), keep_wb->width, keep_wb->height, scanout, hz,
                 need_rotate_hook, keep_wb->buf_num, func);

    return keep_wb;
}

void
_secWbClose (SECWb *wb, const char *func)
{
    SECWbNotifyFuncInfo *info, *tmp;

    XDBG_RETURN_IF_FAIL (wb != NULL);
    XDBG_RETURN_IF_FAIL (keep_wb == wb);

    secWbStop (wb, TRUE);

    XDBG_SECURE (MWB, "wb(%p): %s \n", wb, func);

    _secWbCallNotifyFunc (wb, WB_NOTI_CLOSED, NULL);

    nt_list_for_each_entry_safe (info, tmp, wb->info_list, next)
    {
        nt_list_del (info, wb->info_list, SECWbNotifyFuncInfo, next);
        free (info);
    }

    free (wb);
    keep_wb = NULL;
}

Bool
_secWbStart (SECWb *wb, const char *func)
{
    SECPtr pSec;

    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);

    pSec = SECPTR (wb->pScrn);
    if (pSec->isLcdOff)
    {
        XDBG_ERROR (MWB, "Can't start wb(%p) during DPMS off. \n", wb);
        return FALSE;
    }

    if (wb->status == STATUS_RUNNING)
        return TRUE;

    if (!_secWbOpenDrm (wb))
    {
        XDBG_ERROR (MWB, "open fail. \n");
        return FALSE;
    }

    wb->status = STATUS_RUNNING;

    _secWbCallNotifyFunc (wb, WB_NOTI_START, NULL);

    XDBG_TRACE (MWB, "start: %s \n", func);

    return TRUE;
}

void
_secWbStop (SECWb *wb, Bool close_buf,const char *func)
{
    XDBG_RETURN_IF_FAIL (wb != NULL);

    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE))
    {
        if (wb->rotate_timer)
        {
            TimerFree (wb->rotate_timer);
            wb->rotate_timer = NULL;
        }
        return;
    }

    _secWbCloseDrm (wb, FALSE);

    if (close_buf)
        _secWbCloseDrmDstBuffer (wb);
    else
        _secWbClearDrmDstBuffer (wb);

    wb->status = STATUS_STOP;

    _secWbCallNotifyFunc (wb, WB_NOTI_STOP, NULL);

    XDBG_TRACE (MWB, "stop: %s \n", func);
}

void
secWbPause (SECWb *wb)
{
    XDBG_RETURN_IF_FAIL (wb != NULL);

    if ((wb->status == STATUS_STOP) || (wb->status == STATUS_PAUSE))
    {
        if (wb->rotate_timer)
        {
            TimerFree (wb->rotate_timer);
            wb->rotate_timer = NULL;
        }
        return;
    }

    _secWbCloseDrm (wb, TRUE);

    _secWbCloseDrmDstBuffer (wb);

    wb->status = STATUS_PAUSE;

    _secWbCallNotifyFunc (wb, WB_NOTI_PAUSE, NULL);

    XDBG_TRACE (MWB, "pause: %s, wb(%p)\n", __func__, wb);
}

void
secWbResume (SECWb *wb)
{
    wb->wait_show = -1;
    wb->now_showing = -1;

    if (!secWbStart (wb))
    {
        XDBG_ERROR(MWB, "wb(%p) start fail.%s\n", wb, __func__);
        secWbClose (wb);
        wb = NULL;
    }
    XDBG_TRACE (MWB, "start: %s, wb(%p)\n", __func__, wb);
}

Bool
secWbSetBuffer (SECWb *wb, SECVideoBuf **vbufs, int bufnum)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (wb->status != STATUS_RUNNING, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (vbufs != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (wb->buf_num <= bufnum, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (bufnum <= WB_BUF_MAX, FALSE);

    for (i = 0; i < WB_BUF_MAX; i++)
    {
        if (wb->dst_buf[i])
        {
            XDBG_ERROR (MWB, "already has %d buffers\n", wb->buf_num);
            return FALSE;
        }
    }

    for (i = 0; i < bufnum; i++)
    {
        XDBG_GOTO_IF_FAIL (wb->id == vbufs[i]->id, fail_set_buffer);
        XDBG_GOTO_IF_FAIL (wb->width == vbufs[i]->width, fail_set_buffer);
        XDBG_GOTO_IF_FAIL (wb->height == vbufs[i]->height, fail_set_buffer);
        XDBG_GOTO_IF_FAIL (wb->scanout == vbufs[i]->scanout, fail_set_buffer);

        wb->dst_buf[i] = secUtilVideoBufferRef (vbufs[i]);
        XDBG_GOTO_IF_FAIL (wb->dst_buf[i] != NULL, fail_set_buffer);

        if (!wb->dst_buf[i]->showing && wb->dst_buf[i]->need_reset)
            secUtilClearVideoBuffer (wb->dst_buf[i]);
        else
            wb->dst_buf[i]->need_reset = TRUE;
    }

    wb->buf_num = bufnum;

    return TRUE;

fail_set_buffer:
    for (i = 0; i < WB_BUF_MAX; i++)
    {
        if (wb->dst_buf[i])
        {
            secUtilVideoBufferUnref (wb->dst_buf[i]);
            wb->dst_buf[i] = NULL;
        }
    }

    return FALSE;
}

Bool
secWbSetRotate (SECWb *wb, int rotate)
{
    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL ((rotate % 90) == 0, FALSE);

    if (wb->rotate == rotate)
        return TRUE;

    XDBG_DEBUG (MWB, "rotate(%d) \n", rotate);

    wb->rotate = rotate;

    if (wb->status == STATUS_RUNNING)
    {
        SECModePtr pSecMode = (SECModePtr) SECPTR (wb->pScrn)->pSecMode;

        if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            secWbStop (wb, FALSE);
        else
            secWbStop (wb, TRUE);

        if (!secWbStart (wb))
            return FALSE;
    }

    return TRUE;
}

int
secWbGetRotate (SECWb *wb)
{
    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);

    return wb->rotate;
}

void
secWbSetTvout (SECWb *wb, Bool enable)
{
    XDBG_RETURN_IF_FAIL (wb != NULL);

    enable = (enable > 0) ? TRUE : FALSE;

    XDBG_TRACE (MWB, "tvout(%d) \n", enable);

    wb->tvout = enable;

    if (wb->status == STATUS_RUNNING)
    {
        secWbStop (wb, FALSE);

        if (!secWbStart (wb))
            return;
    }
}

Bool
secWbGetTvout (SECWb *wb)
{
    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);

    return wb->tvout;
}

void
secWbSetSecure (SECWb *wb, Bool secure)
{
    XDBG_RETURN_IF_FAIL (wb != NULL);

    secure = (secure > 0) ? TRUE : FALSE;

    if (wb->secure == secure)
        return;

    XDBG_TRACE (MWB, "secure(%d) \n", secure);

    wb->secure = secure;

    if (wb->status == STATUS_RUNNING)
    {
        secWbStop (wb, TRUE);

        if (!secWbStart (wb))
            return;
    }
}

Bool
secWbGetSecure (SECWb *wb)
{
    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);

    return wb->secure;
}

void
secWbGetSize (SECWb *wb, int *width, int *height)
{
    XDBG_RETURN_IF_FAIL (wb != NULL);

    if (width)
        *width = wb->width;

    if (height)
        *height = wb->height;
}

Bool
secWbCanDequeueBuffer (SECWb *wb)
{
    int i, remain = 0;

    XDBG_RETURN_VAL_IF_FAIL (wb != NULL, FALSE);

    for (i = 0; i< wb->buf_num; i++)
        if (wb->queued[i])
            remain++;

    XDBG_DEBUG (MWB, "buf_num(%d) remain(%d)\n", wb->buf_num, remain);

    return (remain > WB_BUF_MIN) ? TRUE : FALSE;
}

void
secWbQueueBuffer (SECWb *wb, SECVideoBuf *vbuf)
{
    int i;

    XDBG_RETURN_IF_FAIL (wb != NULL);
    XDBG_RETURN_IF_FAIL (vbuf != NULL);
    XDBG_RETURN_IF_FAIL (vbuf->showing == FALSE);

    if (wb->prop_id == -1)
        return;

    for (i = 0; i < wb->buf_num; i++)
        if (wb->dst_buf[i] == vbuf)
        {
            XDBG_DEBUG (MWB, "%d queueing.\n", i);
            _secWbQueue (wb, i);
        }
}

void
secWbAddNotifyFunc (SECWb *wb, SECWbNotify noti, WbNotifyFunc func, void *user_data)
{
    SECWbNotifyFuncInfo *info;

    XDBG_RETURN_IF_FAIL (wb != NULL);

    if (!func)
        return;

    nt_list_for_each_entry (info, wb->info_list, next)
    {
        if (info->func == func)
            return;
    }

    XDBG_DEBUG (MWB, "noti(%d) func(%p) user_data(%p) \n", noti, func, user_data);

    info = calloc (1, sizeof (SECWbNotifyFuncInfo));
    XDBG_RETURN_IF_FAIL (info != NULL);

    info->noti = noti;
    info->func = func;
    info->user_data = user_data;

    if (wb->info_list)
        nt_list_append (info, wb->info_list, SECWbNotifyFuncInfo, next);
    else
        wb->info_list = info;
}

void
secWbRemoveNotifyFunc (SECWb *wb, WbNotifyFunc func)
{
    SECWbNotifyFuncInfo *info;

    XDBG_RETURN_IF_FAIL (wb != NULL);

    if (!func)
        return;

    nt_list_for_each_entry (info, wb->info_list, next)
    {
        if (info->func == func)
        {
            XDBG_DEBUG (MWB, "func(%p) \n", func);
            nt_list_del(info, wb->info_list, SECWbNotifyFuncInfo, next);
            free (info);
            return;
        }
    }
}

SECWb*
secWbGet (void)
{
    return keep_wb;
}

void
secWbDestroy (void)
{
    if (!keep_wb)
        return;

    secWbClose (keep_wb);
}
