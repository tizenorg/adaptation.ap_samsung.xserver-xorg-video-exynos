/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/ioctl.h>
#include <fcntl.h>
#include <errno.h>
#include <stdlib.h>

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_plane.h"
#include "exynos_layer.h"
#include "exynos_video_fourcc.h"
#include "exynos_video_tvout.h"
#include "exynos_video_virtual.h"
#include "exynos_layer_manager.h"
#include <exynos/exynos_drm.h>

//#define DEBUG_REFCNT

#ifdef DEBUG_REFCNT
#define EXYNOS_LAYER_PRINT_REFCNT(b) \
            XDBG_TRACE(MLYR, "layer(%p) ref_cnt(%d) \n", b, b->ref_cnt)
#else
#define EXYNOS_LAYER_PRINT_REFCNT(b)
#endif

typedef struct _NotifyFuncData {
    NotifyFunc func;
    void *user_data;

    struct xorg_list link;
} NotifyFuncData;

struct _EXYNOSLayer {
    ScrnInfoPtr pScrn;

    EXYNOSLayerOutput output;
    EXYNOSLayerPos lpos;

    int plane_id;
    int crtc_id;

    /* for buffer */
    intptr_t fb_id;

    int offset_x;
    int offset_y;

    xRectangle *src;
    xRectangle *dst;

    EXYNOSVideoBuf *vbuf;
    Bool visible;
    Bool need_update;

    /* vblank */
    Bool enable_vblank;
    Bool wait_vblank;
    EXYNOSVideoBuf *wait_vbuf;
    EXYNOSVideoBuf *pending_vbuf;
    EXYNOSVideoBuf *showing_vbuf;

    struct xorg_list noti_data;
    struct xorg_list link;

    Bool onoff;
    int ref_cnt;
    Bool freeze_update;

    /* count */
    unsigned int put_counts;
    OsTimerPtr timer;

    /* for hwc */
    /* drawable id associated with this overlay */
    unsigned int xid;
    DrawablePtr pDraw;
    Bool default_layer;
    /* when a layer is updated by DRI2(and maybe Present) we should forbid update layer (frame buffer) by HWC,
     * HWC have to updata only SRC, DST and zpos.
     * */
    Bool is_updated_dri;

};

static Bool crtc_layers_init;
static struct xorg_list crtc_layers;
static Bool wait_vblank[LAYER_OUTPUT_MAX];

#define LAYER_VBLANK_FLAG 0xFFFF

static CARD32
_countPrint(OsTimerPtr timer, CARD32 now, pointer arg)
{
    EXYNOSLayer *layer = (EXYNOSLayer *) arg;

    if (layer->timer) {
        TimerFree(layer->timer);
        layer->timer = NULL;
    }

    XDBG_DEBUG(MEXA, "crtc(%d) pos(%d) : %d fps. \n",
               layer->crtc_id, layer->lpos, layer->put_counts);

    layer->put_counts = 0;

    return 0;
}

static void
_countFps(EXYNOSLayer * layer)
{
    layer->put_counts++;

    if (layer->timer)
        return;

    layer->timer = TimerSet(NULL, 0, 1000, _countPrint, layer);
}

static void
_exynosLayerInitList(void)
{
    if (!crtc_layers_init) {
        xorg_list_init(&crtc_layers);
        crtc_layers_init = TRUE;
    }
}

static void
_exynosLayerNotify(EXYNOSLayer * layer, int type, void *type_data)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    xorg_list_for_each_entry_safe(data, data_next, &layer->noti_data, link) {
        if (data->func)
            data->func(layer, type, type_data, data->user_data);
    }
}

static int
_GetCrtcIdForOutput(ScrnInfoPtr pScrn, EXYNOSLayerOutput output)
{
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    EXYNOSOutputPrivPtr pOutputPriv = NULL;
    int crtc_id = 0;

    switch (output) {
    case LAYER_OUTPUT_LCD:
        pOutputPriv =
            exynosOutputGetPrivateForConnType(pScrn, DRM_MODE_CONNECTOR_LVDS);
        if (!pOutputPriv)
            pOutputPriv =
                exynosOutputGetPrivateForConnType(pScrn,
                                                  DRM_MODE_CONNECTOR_Unknown);
        if (pOutputPriv && pOutputPriv->mode_encoder)
            crtc_id = pOutputPriv->mode_encoder->crtc_id;
        break;
    case LAYER_OUTPUT_EXT:
        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_HDMI) {
            pOutputPriv =
                exynosOutputGetPrivateForConnType(pScrn,
                                                  DRM_MODE_CONNECTOR_HDMIA);
            if (!pOutputPriv)
                pOutputPriv =
                    exynosOutputGetPrivateForConnType(pScrn,
                                                      DRM_MODE_CONNECTOR_HDMIB);
            if (pOutputPriv && pOutputPriv->mode_encoder)
                crtc_id = pOutputPriv->mode_encoder->crtc_id;
        }
        else if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
            pOutputPriv =
                exynosOutputGetPrivateForConnType(pScrn,
                                                  DRM_MODE_CONNECTOR_VIRTUAL);
            if (pOutputPriv && pOutputPriv->mode_encoder)
                crtc_id = pOutputPriv->mode_encoder->crtc_id;
        }
        break;
    default:
        break;
    }

    XDBG_DEBUG(MLYR, "crtc(%d) for output(%d) \n", crtc_id, output);

    if (crtc_id == 0)
        XDBG_ERROR(MLYR, "no crtc for output(%d) \n", output);

    return crtc_id;
}

static int
_GetCrtcID(EXYNOSLayer * layer)
{
    if (layer->crtc_id > 0)
        return layer->crtc_id;

    layer->crtc_id = _GetCrtcIdForOutput(layer->pScrn, layer->output);

    XDBG_RETURN_VAL_IF_FAIL(layer->crtc_id > 0, 0);

    return layer->crtc_id;
}

static int
_exynosLayerGetPlanePos(EXYNOSLayer * layer, EXYNOSLayerPos lpos)
{
    if (layer->output == LAYER_OUTPUT_LCD) {
        XDBG_DEBUG(MLYR, "lpos(%d) => ppos(%d) (1)\n", lpos,
                   PLANE_POS_3 + lpos);
        return PLANE_POS_3 + lpos;
    }
    else if (layer->output == LAYER_OUTPUT_EXT) {
        if (lpos == -1) {
            XDBG_DEBUG(MLYR, "lpos(%d) => ppos(%d) (2)\n", lpos, PLANE_POS_2);
            return PLANE_POS_2;
        }
        else {
            XDBG_DEBUG(MLYR, "lpos(%d) => ppos(%d) (3)\n", lpos,
                       PLANE_POS_0 + lpos);
            return PLANE_POS_0 + lpos;
        }
    }
    else {
        XDBG_NEVER_GET_HERE(MLYR);
    }

    return -1;
}

static void
_exynosLayerDestroy(EXYNOSLayer * layer)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL(layer != NULL);
    xorg_list_del(&layer->link);

    if (layer->src)
        free(layer->src);
    if (layer->dst)
        free(layer->dst);

    if (layer->wait_vbuf) {
        if (layer->wait_vbuf->vblank_handler) {
            layer->wait_vbuf->vblank_handler(0, 0, 0,
                                             layer->wait_vbuf->vblank_user_data,
                                             TRUE);
            layer->wait_vbuf->vblank_handler = NULL;
            layer->wait_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->wait_vbuf);
    }
    if (layer->pending_vbuf) {
        if (layer->pending_vbuf->vblank_handler) {
            layer->pending_vbuf->vblank_handler(0, 0, 0,
                                                layer->pending_vbuf->
                                                vblank_user_data, TRUE);
            layer->pending_vbuf->vblank_handler = NULL;
            layer->pending_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->pending_vbuf);
    }
    if (layer->showing_vbuf) {
        if (layer->showing_vbuf->vblank_handler) {
            layer->showing_vbuf->vblank_handler(0, 0, 0,
                                                layer->showing_vbuf->
                                                vblank_user_data, TRUE);
            layer->showing_vbuf->vblank_handler = NULL;
            layer->showing_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->showing_vbuf);
    }
    if (layer->vbuf) {
        if (layer->vbuf->vblank_handler) {
            layer->vbuf->vblank_handler(0, 0, 0, layer->vbuf->vblank_user_data,
                                        TRUE);
            layer->vbuf->vblank_handler = NULL;
            layer->vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->vbuf);
        layer->vbuf = NULL;
    }

    XDBG_TRACE(MLYR, "layer(%p) destroyed. \n", layer);
    EXYNOS_LAYER_PRINT_REFCNT(layer);

    _exynosLayerNotify(layer, LAYER_DESTROYED, NULL);

    xorg_list_for_each_entry_safe(data, data_next, &layer->noti_data, link) {
        xorg_list_del(&data->link);
        free(data);
    }

    if (layer->plane_id > 0)
        exynosPlaneFreeId(layer->plane_id);

    free(layer);
}

void
exynosLayerDestroy(EXYNOSLayer * layer)
{
    _exynosLayerDestroy(layer);
}

static void
_exynosLayerWatchVblank(EXYNOSLayer * layer)
{
    CARD64 ust, msc, target_msc;
    intptr_t pipe, flip = 1;
    EXYNOSPtr pExynos = EXYNOSPTR(layer->pScrn);

    /* if lcd is off, do not request vblank information */
#ifdef NO_CRTC_MODE
    if (pExynos->isCrtcOn == FALSE)
        return;
    else
#endif                          //NO_CRTC_MODE
//    if (pExynos->isLcdOff)
//    {
//        XDBG_DEBUG(MLYR, "pExynos->isLcdOff (%d)\n", pExynos->isLcdOff);
//        return;
//    }

        pipe = exynosDisplayCrtcPipe(layer->pScrn, _GetCrtcID(layer));

    layer->wait_vblank = TRUE;

    if (wait_vblank[pipe])
        return;

    wait_vblank[pipe] = TRUE;

    if (!exynosDisplayGetCurMSC(layer->pScrn, pipe, &ust, &msc))
        XDBG_WARNING(MLYR, "fail to get current_msc.\n");

    target_msc = msc + 1;

    XDBG_TRACE(MLYR, "layer(%p) wait vblank : cur(%lld) target(%lld). \n",
               layer, msc, target_msc);

    if (!exynosDisplayVBlank
        (layer->pScrn, pipe, &target_msc, flip, VBLANK_INFO_PLANE,
         (void *) pipe))
        XDBG_WARNING(MLYR, "fail to Vblank.\n");
}

static Bool
_exynosLayerShowInternal(EXYNOSLayer * layer, Bool need_update)
{
    int crtc_id, plane_pos;

    XDBG_RETURN_VAL_IF_FAIL(layer->fb_id > 0, FALSE);

    crtc_id = _GetCrtcID(layer);
    plane_pos = _exynosLayerGetPlanePos(layer, layer->lpos);

    if (!exynosPlaneShow(layer->plane_id, crtc_id,
                         layer->src->x, layer->src->y,
                         layer->src->width, layer->src->height,
                         layer->offset_x + layer->dst->x,
                         layer->offset_y + layer->dst->y,
                         layer->dst->width, layer->dst->height,
                         plane_pos, need_update))
        return FALSE;
    layer->need_update = FALSE;

    return TRUE;
}

static void
_exynosLayerGetBufferID(EXYNOSLayer * layer, EXYNOSVideoBuf * vbuf)
{
    EXYNOSModePtr pExynosMode;
    unsigned int drmfmt;
    unsigned int handles[4] = { 0, };
    unsigned int pitches[4] = { 0, };
    unsigned int offsets[4] = { 0, };
    int i;

    if (vbuf->fb_id > 0)
        return;

    pExynosMode = (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;
    drmfmt = exynosUtilGetDrmFormat(vbuf->id);

    for (i = 0; i < PLANAR_CNT; i++) {
        handles[i] = (unsigned int) vbuf->handles[i];
        pitches[i] = (unsigned int) vbuf->pitches[i];
        offsets[i] = (unsigned int) vbuf->offsets[i];
    }

    if (drmModeAddFB2(pExynosMode->fd, vbuf->width, vbuf->height, drmfmt,
                      handles, pitches, offsets, (uint32_t *) & vbuf->fb_id, 0))
    {
        XDBG_ERRNO(MLYR,
                   "drmModeAddFB2 failed. handles(%d %d %d) pitches(%d %d %d) offsets(%d %d %d) '%c%c%c%c'\n",
                   handles[0], handles[1], handles[2], pitches[0], pitches[1],
                   pitches[2], offsets[0], offsets[1], offsets[2],
                   FOURCC_STR(drmfmt));
    }

    XDBG_DEBUG(MVBUF,
               "layer(%p) vbuf(%" PRIuPTR ") fb_id(%" PRIdPTR ") added. \n",
               layer, vbuf->stamp, vbuf->fb_id);
}

Bool
exynosLayerSupport(ScrnInfoPtr pScrn, EXYNOSLayerOutput output,
                   EXYNOSLayerPos lpos, unsigned int id)
{
    EXYNOSModePtr pExynosMode;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(output < LAYER_OUTPUT_MAX, FALSE);

    pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;

    if (output == LAYER_OUTPUT_EXT && lpos == LAYER_LOWER1) {
        if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_HDMI) {
            if (id == FOURCC_SN12 || id == FOURCC_ST12)
                return TRUE;
            else
                return FALSE;
        }
        else if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
            if (id == FOURCC_SN12 || id == FOURCC_RGB32)
                return TRUE;
            else
                return FALSE;
        }
    }

    return (id == FOURCC_RGB32 || id == FOURCC_SR32) ? TRUE : FALSE;
}

EXYNOSLayer *
exynosLayerFind(EXYNOSLayerOutput output, EXYNOSLayerPos lpos)
{
    EXYNOSLayer *layer = NULL, *layer_next = NULL;

    XDBG_RETURN_VAL_IF_FAIL(output < LAYER_OUTPUT_MAX, NULL);

    _exynosLayerInitList();

    xorg_list_for_each_entry_safe(layer, layer_next, &crtc_layers, link) {
        if (layer->output == output && layer->lpos == lpos)
            return layer;
    }

    return NULL;
}

EXYNOSLayer *
exynosLayerFindByDraw(DrawablePtr pDraw)
{
    EXYNOSLayer *layer = NULL, *layer_next = NULL;

    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, NULL);

    _exynosLayerInitList();

    xorg_list_for_each_entry_safe(layer, layer_next, &crtc_layers, link) {
        if (layer->pDraw == pDraw)
            return layer;
    }

    return NULL;
}

void
exynosLayerDestroyAll(void)
{
    EXYNOSLayer *layer = NULL, *layer_next = NULL;

    _exynosLayerInitList();

    xorg_list_for_each_entry_safe(layer, layer_next, &crtc_layers, link) {
        _exynosLayerDestroy(layer);
    }
}

void
exynosLayerShowAll(ScrnInfoPtr pScrn, EXYNOSLayerOutput output)
{
    int crtc_id = _GetCrtcIdForOutput(pScrn, output);

    exynosPlaneShowAll(crtc_id);
}

EXYNOSLayer *
exynosLayerCreate(ScrnInfoPtr pScrn, EXYNOSLayerOutput output,
                  EXYNOSLayerPos lpos)
{
    EXYNOSLayer *layer;

    XDBG_RETURN_VAL_IF_FAIL(pScrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL(output < LAYER_OUTPUT_MAX, NULL);
/* Temporary solution */
#ifdef LAYER_MANAGER
    if (lpos != FOR_LAYER_MNG) {
        XDBG_WARNING(MLYR,
                     "Layer manager is enable, avoid direct create layers\n");
        return ((EXYNOSLayer *)
                exynosLayerMngTempGetHWLayer(pScrn, output, lpos));
    }
#endif

#if defined (HAVE_HWC_H) && !defined(LAYER_MANAGER)
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

    if (!pExynos->use_hwc)

        XDBG_RETURN_VAL_IF_FAIL(lpos != LAYER_DEFAULT, NULL);
#endif
#ifndef LAYER_MANAGER
    layer = exynosLayerFind(output, lpos);
    if (layer) {
        XDBG_ERROR(MLYR, "layer(%p) already is at output(%d) lpos(%d). \n",
                   layer, output, lpos);

        return NULL;
    }
#endif
    layer = calloc(sizeof(EXYNOSLayer), 1);
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);

    layer->pScrn = pScrn;
    layer->output = output;
    layer->lpos = lpos;

    layer->plane_id = exynosPlaneGetID();
    if (layer->plane_id < 0) {
        free(layer);
        return NULL;
    }

    layer->ref_cnt = 1;
    xorg_list_init(&layer->noti_data);

    _exynosLayerInitList();

    xorg_list_add(&layer->link, &crtc_layers);

    XDBG_TRACE(MLYR, "layer(%p) output(%d) lpos(%d) created. \n", layer, output,
               lpos);
    EXYNOS_LAYER_PRINT_REFCNT(layer);

    return layer;
}

EXYNOSLayer *
exynosLayerRef(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);

    layer->ref_cnt++;

    EXYNOS_LAYER_PRINT_REFCNT(layer);

    return layer;
}

void
exynosLayerUnref(EXYNOSLayer * layer)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    layer->ref_cnt--;

    EXYNOS_LAYER_PRINT_REFCNT(layer);

    if (layer->ref_cnt == 0) {
        exynosLayerHide(layer);
#ifdef LAYER_MANAGER
        if (exynosLayerMngTempDestroyHWLayer(layer)) {
            return;
        }
#else
        _exynosLayerDestroy(layer);
#endif
    }
}

void
exynosLayerAddNotifyFunc(EXYNOSLayer * layer, NotifyFunc func, void *user_data)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL(layer != NULL);
    XDBG_RETURN_IF_FAIL(func != NULL);

    xorg_list_for_each_entry_safe(data, data_next, &layer->noti_data, link) {
        if (data->func == func && data->user_data == user_data)
            return;
    }

    data = calloc(sizeof(NotifyFuncData), 1);
    XDBG_RETURN_IF_FAIL(data != NULL);

    data->func = func;
    data->user_data = user_data;

    xorg_list_add(&data->link, &layer->noti_data);
}

void
exynosLayerRemoveNotifyFunc(EXYNOSLayer * layer, NotifyFunc func)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL(layer != NULL);
    XDBG_RETURN_IF_FAIL(func != NULL);

    xorg_list_for_each_entry_safe(data, data_next, &layer->noti_data, link) {
        if (data->func == func) {
            xorg_list_del(&data->link);
            free(data);
        }
    }
}

Bool
exynosLayerExistNotifyFunc(EXYNOSLayer * layer, NotifyFunc func)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(func != NULL, FALSE);

    xorg_list_for_each_entry_safe(data, data_next, &layer->noti_data, link) {
        if (data->func == func) {
            return TRUE;
        }
    }
    return FALSE;
}

Bool
exynosLayerIsVisible(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);

    return layer->visible;
}

void
exynosLayerShow(EXYNOSLayer * layer)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);
    XDBG_RETURN_IF_FAIL(layer->src != NULL);
    XDBG_RETURN_IF_FAIL(layer->dst != NULL);
    XDBG_RETURN_IF_FAIL(layer->fb_id > 0);
    if (!layer->src || !layer->dst) {
        EXYNOSVideoBuf *grab_vbuf = NULL;

        if (layer->vbuf)
            grab_vbuf = layer->vbuf;
        else if (layer->showing_vbuf)
            grab_vbuf = layer->showing_vbuf;
        else if (layer->wait_vbuf)
            grab_vbuf = layer->wait_vbuf;
        else if (layer->pending_vbuf)
            grab_vbuf = layer->pending_vbuf;
        else
            return;
        {
            xRectangle src = {.x = 0,.y = 0,.width = grab_vbuf->width,.height =
                    grab_vbuf->height
            };
            xRectangle dst = {.x = 0,.y = 0,.width = grab_vbuf->width,.height =
                    grab_vbuf->height
            };
            exynosLayerSetRect(layer, &src, &dst);
        }
    }

#ifndef HWC_ENABLE_REDRAW_LAYER
    if (layer->visible)
        return;
#endif

#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
        layer->visible = TRUE;
        XDBG_TRACE(MLYR, "layer(%p) shown. \n", layer);
        return;
    }
#endif
#ifndef HWC_ENABLE_REDRAW_LAYER
    if (!_exynosLayerShowInternal(layer, FALSE))
        return;
#else
    if (!_exynosLayerShowInternal(layer, TRUE))
        return;
#endif

    if (layer->enable_vblank)
        _exynosLayerWatchVblank(layer);

    layer->visible = TRUE;

    XDBG_TRACE(MLYR, "layer(%p) shown. \n", layer);

    _exynosLayerNotify(layer, LAYER_SHOWN, (void *) layer->fb_id);
}

void
exynosLayerClearQueue(EXYNOSLayer * layer)
{

#if 1
    if (layer->wait_vbuf && VBUF_IS_VALID(layer->wait_vbuf)) {
        XDBG_DEBUG(MVBUF, "layer(%p) <-- %s (%" PRIuPTR ",%d,%d) \n", layer,
                   (layer->output == LAYER_OUTPUT_LCD) ? "LCD" : "TV",
                   layer->wait_vbuf->stamp,
                   VBUF_IS_CONVERTING(layer->wait_vbuf),
                   layer->wait_vbuf->showing);
        layer->wait_vbuf->showing = FALSE;
        if (layer->wait_vbuf->vblank_handler) {
            layer->wait_vbuf->vblank_handler(0, 0, 0,
                                             layer->wait_vbuf->vblank_user_data,
                                             TRUE);
            layer->wait_vbuf->vblank_handler = NULL;
            layer->wait_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->wait_vbuf);
    }

    if (layer->pending_vbuf && VBUF_IS_VALID(layer->pending_vbuf)) {
        layer->pending_vbuf->showing = FALSE;
        if (layer->pending_vbuf->vblank_handler) {
            layer->pending_vbuf->vblank_handler(0, 0, 0,
                                                layer->pending_vbuf->
                                                vblank_user_data, TRUE);
            layer->pending_vbuf->vblank_handler = NULL;
            layer->pending_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->pending_vbuf);
    }
/*
    if (layer->showing_vbuf && VBUF_IS_VALID (layer->showing_vbuf))
    {
        layer->showing_vbuf->showing = FALSE;
        XDBG_DEBUG (MVBUF, "layer(%p) <-- %s (%"PRIuPTR",%d,%d) \n", layer,
                    (layer->output==LAYER_OUTPUT_LCD)?"LCD":"TV",
                    layer->showing_vbuf->stamp, VBUF_IS_CONVERTING (layer->showing_vbuf),
                    layer->showing_vbuf->showing);
        if (layer->showing_vbuf->vblank_handler)
        {
            layer->showing_vbuf->vblank_handler(0, 0, 0, layer->showing_vbuf->vblank_user_data, TRUE);
            layer->showing_vbuf->vblank_handler = NULL;
            layer->showing_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref (layer->showing_vbuf);
    }
*/
    if (layer->showing_vbuf && VBUF_IS_VALID(layer->showing_vbuf)) {
//        layer->showing_vbuf->showing = FALSE;
        if (layer->showing_vbuf->vblank_handler) {
            layer->showing_vbuf->vblank_handler(0, 0, 0,
                                                layer->showing_vbuf->
                                                vblank_user_data, TRUE);
            layer->showing_vbuf->vblank_handler = NULL;
            layer->showing_vbuf->vblank_user_data = NULL;
        }
//        exynosUtilVideoBufferUnref (layer->showing_vbuf);
    }

    if (layer->vbuf) {
        if (layer->vbuf->vblank_handler) {
            layer->vbuf->vblank_handler(0, 0, 0, layer->vbuf->vblank_user_data,
                                        TRUE);
            layer->vbuf->vblank_handler = NULL;
            layer->vbuf->vblank_user_data = NULL;
        }
    }

    layer->wait_vbuf = NULL;
    layer->pending_vbuf = NULL;
//    layer->showing_vbuf = NULL;
#endif
    if (layer->plane_id > 0)
        exynosPlaneFlushFBId(layer->plane_id);

    XDBG_TRACE(MLYR, "layer(%p) flush. \n", layer);

}

void
exynosLayerHide(EXYNOSLayer * layer)
{

    XDBG_RETURN_IF_FAIL(layer != NULL);

    if (!layer->visible || layer->ref_cnt > 1)
        return;
#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
        layer->visible = FALSE;
        XDBG_TRACE(MLYR, "layer(%p) hidden. \n", layer);
        return;
    }
#endif
    if (!exynosPlaneHide(layer->plane_id))
        return;

    if (layer->wait_vbuf && VBUF_IS_VALID(layer->wait_vbuf)) {
        layer->wait_vbuf->showing = FALSE;
        XDBG_DEBUG(MVBUF, "layer(%p) <-- %s (%" PRIuPTR ",%d,%d) \n", layer,
                   (layer->output == LAYER_OUTPUT_LCD) ? "LCD" : "TV",
                   layer->wait_vbuf->stamp,
                   VBUF_IS_CONVERTING(layer->wait_vbuf),
                   layer->wait_vbuf->showing);
        if (layer->wait_vbuf->vblank_handler) {
            layer->wait_vbuf->vblank_handler(0, 0, 0,
                                             layer->wait_vbuf->vblank_user_data,
                                             TRUE);
            layer->wait_vbuf->vblank_handler = NULL;
            layer->wait_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->wait_vbuf);
    }

    if (layer->pending_vbuf && VBUF_IS_VALID(layer->pending_vbuf)) {
        layer->pending_vbuf->showing = FALSE;
        if (layer->pending_vbuf->vblank_handler) {
            layer->pending_vbuf->vblank_handler(0, 0, 0,
                                                layer->pending_vbuf->
                                                vblank_user_data, TRUE);
            layer->pending_vbuf->vblank_handler = NULL;
            layer->pending_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->pending_vbuf);
    }

    if (layer->showing_vbuf && VBUF_IS_VALID(layer->showing_vbuf)) {
        layer->showing_vbuf->showing = FALSE;
        XDBG_DEBUG(MVBUF, "layer(%p) <-- %s (%" PRIuPTR ",%d,%d) \n", layer,
                   (layer->output == LAYER_OUTPUT_LCD) ? "LCD" : "TV",
                   layer->showing_vbuf->stamp,
                   VBUF_IS_CONVERTING(layer->showing_vbuf),
                   layer->showing_vbuf->showing);
        if (layer->showing_vbuf->vblank_handler) {
            layer->showing_vbuf->vblank_handler(0, 0, 0,
                                                layer->showing_vbuf->
                                                vblank_user_data, TRUE);
            layer->showing_vbuf->vblank_handler = NULL;
            layer->showing_vbuf->vblank_user_data = NULL;
        }
        exynosUtilVideoBufferUnref(layer->showing_vbuf);
    }

    layer->showing_vbuf = NULL;
    layer->pending_vbuf = NULL;
    layer->wait_vbuf = NULL;
    layer->wait_vblank = FALSE;
    layer->visible = FALSE;
    layer->crtc_id = 0;

    XDBG_TRACE(MLYR, "layer(%p) hidden. \n", layer);

    _exynosLayerNotify(layer, LAYER_HIDDEN, (void *) layer->fb_id);
}

void
exynosLayerFreezeUpdate(EXYNOSLayer * layer, Bool enable)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    layer->freeze_update = enable;

    XDBG_TRACE(MLYR, "layer(%p) freeze %d. \n", layer, enable);

    if (layer->plane_id > 0)
        exynosPlaneFreezeUpdate(layer->plane_id, enable);
}

Bool
exynosLayerIsNeedUpdate(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    return layer->need_update;
}

void
exynosLayerUpdate(EXYNOSLayer * layer)
{
    XDBG_DEBUG(MLYR, "E. layer %p\n", layer);

    XDBG_RETURN_IF_FAIL(layer != NULL);
    XDBG_RETURN_IF_FAIL(layer->fb_id > 0);

    if (!layer->visible) {
        XDBG_DEBUG(MLYR, "Q. layer->visible == %s.\n",
                   layer->visible ? "TRUE" : "FALSE");
        return;
    }

    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR(layer->pScrn);
    EXYNOSCrtcPrivPtr pCrtcPriv = NULL;
    int c;

    for (c = 0; c < pCrtcConfig->num_crtc; c++) {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
        EXYNOSCrtcPrivPtr pTemp = pCrtc->driver_private;

        if (pTemp == NULL)
            continue;
        if (pTemp->mode_crtc && pTemp->mode_crtc->crtc_id == layer->crtc_id) {
            pCrtcPriv = pTemp;
            break;
        }
    }

    if (!pCrtcPriv || pCrtcPriv->bAccessibility) {
        if (pCrtcPriv)
            XDBG_DEBUG(MLYR,
                       "Q. pCrtcPriv == %p pCrtcPriv->bAccessibility == %s.\n",
                       pCrtcPriv, pCrtcPriv->bAccessibility ? "TRUE" : "FALSE");
        else
            XDBG_DEBUG(MLYR, "Q, pCrtcPriv == NULL \n");

        return;
    }
#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return;
#endif
    if (!_exynosLayerShowInternal(layer, TRUE)) {
        XDBG_DEBUG(MLYR, "Q. _exynosLayerShowInternal == FALSE.\n");
        return;
    }
    XDBG_DEBUG(MLYR, "Q.\n");
}

void
exynosLayerTurn(EXYNOSLayer * layer, Bool onoff, Bool user)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    exynosPlaneTrun(layer->plane_id, onoff, user);
}

Bool
exynosLayerTurnStatus(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);

    return exynosPlaneTrunStatus(layer->plane_id);
}

void
exynosLayerEnableVBlank(EXYNOSLayer * layer, Bool enable)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    layer->enable_vblank = (enable) ? TRUE : FALSE;
}

Bool
exynosLayerSetOffset(EXYNOSLayer * layer, int x, int y)
{

    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);

    if (layer->offset_x == x && layer->offset_y == y)
        return TRUE;

    /* display controller restriction. x+width=2's mutiple */
    XDBG_TRACE(MLYR, "layer(%p) offset(%d,%d => %d,%d).\n",
               layer, x, y, x & (~0x1), y);
    layer->offset_x = x & (~0x1);
    layer->offset_y = y;
#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return TRUE;
#endif
    layer->need_update = TRUE;
    if (exynosLayerIsVisible(layer) && !layer->freeze_update) {
        int crtc_id = _GetCrtcID(layer);
        int plane_pos = _exynosLayerGetPlanePos(layer, layer->lpos);

        if (!exynosPlaneShow(layer->plane_id, crtc_id,
                             layer->src->x, layer->src->y,
                             layer->src->width, layer->src->height,
                             layer->offset_x + layer->dst->x,
                             layer->offset_y + layer->dst->y,
                             layer->dst->width, layer->dst->height,
                             plane_pos, FALSE))
            return FALSE;
        layer->need_update = FALSE;
    }

    return TRUE;
}

void
exynosLayerGetOffset(EXYNOSLayer * layer, int *x, int *y)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    if (x)
        *x = layer->offset_x;
    if (y)
        *y = layer->offset_y;
}

Bool
exynosLayerSetOutput(EXYNOSLayer * layer, EXYNOSLayerOutput output)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(output >= LAYER_OUTPUT_LCD &&
                            output < LAYER_OUTPUT_MAX, FALSE);
    layer->output = output;
    layer->crtc_id = 0;
    return TRUE;
}

Bool
exynosLayerSetPos(EXYNOSLayer * layer, EXYNOSLayerPos lpos)
{

    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(lpos >= LAYER_NONE && lpos < LAYER_MAX, FALSE);

#if 1
    EXYNOSModePtr pExynosMode;

    pExynosMode = (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;
    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
        layer->lpos = lpos;
        return TRUE;
    }
#endif

    if (layer->lpos == lpos)
        return TRUE;

    if (exynosLayerFind(layer->output, lpos) && lpos != LAYER_NONE)
        return FALSE;

    layer->need_update = TRUE;
    if (exynosLayerIsVisible(layer) && !layer->freeze_update) {
        if (lpos == LAYER_NONE) {
            if (!exynosPlaneHide(layer->plane_id))
                return FALSE;

            layer->visible = FALSE;
            layer->crtc_id = 0;
        }
        else {
            int crtc_id = _GetCrtcID(layer);
            int plane_pos = _exynosLayerGetPlanePos(layer, lpos);

            if (!exynosPlaneShow(layer->plane_id, crtc_id,
                                 layer->src->x, layer->src->y,
                                 layer->src->width, layer->src->height,
                                 layer->offset_x + layer->dst->x,
                                 layer->offset_y + layer->dst->y,
                                 layer->dst->width, layer->dst->height,
                                 plane_pos, FALSE))
                return FALSE;
            layer->need_update = FALSE;
        }
    }

    XDBG_TRACE(MLYR, "layer(%p) lpos(%d). \n", layer, lpos);

    layer->lpos = lpos;

    return TRUE;
}

Bool
exynosLayerSwapPos(EXYNOSLayer * layer1, EXYNOSLayer * layer2)
{
    EXYNOSLayer *lower, *upper;
    EXYNOSLayerPos upper_lpos, lower_lpos;

    XDBG_RETURN_VAL_IF_FAIL(layer1 != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(layer2 != NULL, FALSE);

    XDBG_TRACE(MLYR, "layer1(%p) layer2(%p). \n", layer1, layer2);

    lower = (layer2->lpos < layer1->lpos) ? layer2 : layer1;
    upper = (layer2->lpos < layer1->lpos) ? layer1 : layer2;

    upper_lpos = upper->lpos;
    lower_lpos = lower->lpos;

    exynosLayerSetPos(upper, LAYER_NONE);
    exynosLayerSetPos(lower, upper_lpos);
    exynosLayerSetPos(upper, lower_lpos);

    return TRUE;
}

EXYNOSLayerPos
exynosLayerGetPos(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, 0);

    return layer->lpos;
}

Bool
exynosLayerSetRect(EXYNOSLayer * layer, xRectangle *src, xRectangle *dst)
{

    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst != NULL, FALSE);

    if (!layer->src)
        layer->src = calloc(sizeof(xRectangle), 1);

    XDBG_RETURN_VAL_IF_FAIL(layer->src != NULL, FALSE);

    if (!layer->dst)
        layer->dst = calloc(sizeof(xRectangle), 1);

    XDBG_RETURN_VAL_IF_FAIL(layer->dst != NULL, FALSE);

    if (!memcmp(layer->src, src, sizeof(xRectangle)) &&
        !memcmp(layer->dst, dst, sizeof(xRectangle)))
        return TRUE;

    *layer->src = *src;
    *layer->dst = *dst;

    XDBG_TRACE(MLYR, "layer(%p) src(%d,%d %dx%d) dst(%d,%d %dx%d). \n",
               layer, src->x, src->y, src->width, src->height,
               dst->x, dst->y, dst->width, dst->height);
#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return TRUE;
#endif
    if (layer->pending_vbuf && VBUF_IS_VALID(layer->pending_vbuf)) {
        layer->pending_vbuf->showing = FALSE;
        exynosUtilVideoBufferUnref(layer->pending_vbuf);
        layer->pending_vbuf = NULL;
    }

    layer->need_update = TRUE;
    if (exynosLayerIsVisible(layer) && !layer->freeze_update) {
        int plane_pos = _exynosLayerGetPlanePos(layer, layer->lpos);

        if (!exynosPlaneShow(layer->plane_id, _GetCrtcID(layer),
                             src->x, src->y, src->width, src->height,
                             layer->offset_x + dst->x,
                             layer->offset_y + dst->y,
                             dst->width, dst->height, plane_pos, FALSE))
            return FALSE;
        layer->need_update = FALSE;
    }

    return TRUE;
}

void
exynosLayerGetRect(EXYNOSLayer * layer, xRectangle *src, xRectangle *dst)
{
    XDBG_RETURN_IF_FAIL(layer != NULL);

    if (src && layer->src)
        *src = *layer->src;

    if (dst && layer->dst)
        *dst = *layer->dst;
}

Bool
exynosLayerIsPanding(EXYNOSLayer * layer)
{
    if (layer->wait_vbuf && layer->pending_vbuf) {
        return TRUE;
    }
    return FALSE;
}

int
exynosLayerSetBuffer(EXYNOSLayer * layer, EXYNOSVideoBuf * vbuf)
{
    XDBG_DEBUG(MLYR, "E. layer %p vbuf %p\n", layer, vbuf);
    unsigned int fb_id = 0;

    if (!layer) {
        XDBG_ERROR(MLYR, "Layer is NULL\n");
        XDBG_DEBUG(MLYR, "Q. ret = 0(fb_id)\n");
        return 0;
    }
    if (!VBUF_IS_VALID(vbuf)) {
        XDBG_ERROR(MLYR, "vbuf %p is not valid\n", vbuf);
        XDBG_DEBUG(MLYR, "Q. ret = 0(fb_id)\n");
        return 0;
    }

    if (!exynosLayerSupport(layer->pScrn, layer->output, layer->lpos, vbuf->id)) {
        XDBG_ERROR(MLYR,
                   "fail : layer(%p) output(%d) lpos(%d) vbuf(%c%c%c%c)\n",
                   layer, layer->output, layer->lpos, FOURCC_STR(vbuf->id));
        XDBG_DEBUG(MLYR, "Q. ret = 0(fb_id)\n");
        return 0;
    }

#if 1
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(layer->pScrn)->pExynosMode;

    if (layer->output == LAYER_OUTPUT_EXT &&
        pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
        if (layer->enable_vblank == TRUE)
            layer->enable_vblank = FALSE;

        XDBG_TRACE(MLYR, "layer(%p) vbuf('%c%c%c%c', %dx%d, %d,%d %dx%d)\n",
                   layer, FOURCC_STR(vbuf->id), vbuf->width, vbuf->height,
                   vbuf->crop.x, vbuf->crop.y, vbuf->crop.width,
                   vbuf->crop.height);

        if (layer->vbuf)
            exynosUtilVideoBufferUnref(layer->vbuf);

        layer->vbuf = exynosUtilVideoBufferRef(vbuf);
        layer->fb_id = 1;

        _exynosLayerNotify(layer, LAYER_BUF_CHANGED, vbuf);

        return layer->fb_id;
    }
#endif
#if 0
    if (layer->wait_vbuf && layer->pending_vbuf) {
        XDBG_TRACE(MLYR, "pending_vbuf(%" PRIuPTR ") exists.\n",
                   layer->pending_vbuf->stamp);
        return 0;
    }
#endif

    _exynosLayerGetBufferID(layer, vbuf);
    if (vbuf->fb_id <= 0) {
        XDBG_ERROR(MLYR, "Can't set fb_id to vbuf %p\n", vbuf);
        XDBG_DEBUG(MLYR, "Q. ret = 0(fb_id)\n");
    }

    if (layer->pending_vbuf) {
        if (VBUF_IS_VALID(layer->pending_vbuf)) {
            layer->pending_vbuf->showing = FALSE;
            if (layer->pending_vbuf->vblank_handler) {
                layer->pending_vbuf->vblank_handler(0, 0, 0,
                                                    layer->pending_vbuf->
                                                    vblank_user_data, TRUE);
                layer->pending_vbuf->vblank_handler = NULL;
                layer->pending_vbuf->vblank_user_data = NULL;
            }
            exynosUtilVideoBufferUnref(layer->pending_vbuf);
            layer->pending_vbuf = NULL;
        }
        else {
            XDBG_NEVER_GET_HERE(MLYR);
        }
    }

    fb_id = exynosPlaneGetBuffer(layer->plane_id, NULL, vbuf);
    if (fb_id == 0) {
        fb_id = exynosPlaneAddBuffer(layer->plane_id, vbuf);
        if (fb_id <= 0) {
            XDBG_ERROR(MLYR, "Can't add vbuf %p to plane %d\n", vbuf,
                       layer->plane_id);
            XDBG_DEBUG(MLYR, "Q. ret = 0(fb_id)\n");
            return 0;
        }
    }

    if (layer->wait_vbuf && !layer->pending_vbuf) {
        layer->pending_vbuf = exynosUtilVideoBufferRef(vbuf);
        XDBG_RETURN_VAL_IF_FAIL((layer->pending_vbuf != NULL), 0);
        layer->pending_vbuf->showing = TRUE;
        XDBG_TRACE(MLYR, "pending vbuf(%" PRIuPTR ").\n",
                   layer->pending_vbuf->stamp);
        XDBG_DEBUG(MLYR, "Q. ret = %d(fb_id)\n", vbuf->fb_id);
        return vbuf->fb_id;
    }

    layer->fb_id = fb_id;
    if (!exynosPlaneAttach(layer->plane_id, 0, vbuf)) {
        XDBG_ERROR(MLYR, "Can't attach vbuf %p to plane %d\n", vbuf,
                   layer->plane_id);
        XDBG_DEBUG(MLYR, "Q. exynosPlaneAttach == FALSE, ret = 0(fb_id)\n");
        return 0;
    }

    if (exynosLayerIsVisible(layer) && !layer->freeze_update)
        if (!_exynosLayerShowInternal(layer, TRUE)) {
            XDBG_DEBUG(MLYR,
                       "Q. _exynosLayerShowInternal == FALSE, ret = 0(fb_id)\n");
            return 0;
        }

    if (layer->enable_vblank) {
        layer->wait_vbuf = exynosUtilVideoBufferRef(vbuf);
        XDBG_RETURN_VAL_IF_FAIL((layer->wait_vbuf != NULL), 0);
        layer->wait_vbuf->showing = TRUE;
        XDBG_TRACE(MLYR, "layer(%p) --> %s (%" PRIuPTR ",%d,%d) \n", layer,
                   (layer->output == LAYER_OUTPUT_LCD) ? "LCD" : "TV",
                   layer->wait_vbuf->stamp,
                   VBUF_IS_CONVERTING(layer->wait_vbuf),
                   layer->wait_vbuf->showing);

        if (exynosLayerIsVisible(layer)) {
            XDBG_TRACE(MLYR, "layer(%p) fb_id(%d) attached. \n", layer, fb_id);
            _exynosLayerWatchVblank(layer);
        }
    }

    if (layer->vbuf) {
//        if (layer->vbuf->vblank_handler)
//        {
//            layer->vbuf->vblank_handler(0, 0, 0, layer->vbuf->vblank_user_data, TRUE);
//            layer->vbuf->vblank_handler = NULL;
//            layer->vbuf->vblank_user_data = NULL;
//        }
        exynosUtilVideoBufferUnref(layer->vbuf);
    }
    layer->vbuf = exynosUtilVideoBufferRef(vbuf);

    _exynosLayerNotify(layer, LAYER_BUF_CHANGED, vbuf);
    XDBG_DEBUG(MLYR, "Q. ret = %d(fb_id)\n", fb_id);
    return fb_id;
}

EXYNOSVideoBuf *
exynosLayerGetBuffer(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);

    if (layer->showing_vbuf && layer->dst && layer->visible)
        return layer->showing_vbuf;
    else if (layer->wait_vbuf)
        return layer->wait_vbuf;
    else if (layer->pending_vbuf)
        return layer->pending_vbuf;
    else if (layer->vbuf)
        return (layer->vbuf);

    return NULL;
}

DrawablePtr
exynosLayerGetDraw(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);
    return layer->pDraw;
}

EXYNOSVideoBuf *
exynosLayerGetMatchBuf(EXYNOSLayer * layer, tbm_bo bo)
{
#if 0
    EXYNOSVideoBuf *pVbuf = NULL;
    EXYNOSFbBoDataPtr bo_data = NULL;
    int ret = tbm_bo_get_user_data(bo, TBM_BO_DATA_FB, (void * *) &bo_data);

    if (ret == 0 || bo_data == NULL) {
        return NULL;
    }

    if (layer->showing_vbuf && layer->showing_vbuf->fb_id == bo_data->fb_id)
        pVbuf = layer->showing_vbuf;

    else if (layer->wait_vbuf && layer->wait_vbuf->fb_id == bo_data->fb_id)
        pVbuf = layer->wait_vbuf;

    else if (layer->pending_vbuf &&
             layer->pending_vbuf->fb_id == bo_data->fb_id)
        pVbuf = layer->pending_vbuf;

    return pVbuf;
#endif
    return NULL;

}

/*
* we should check new drawable with previous drawable.
* but "tbo" which is attached to drawable could be different (for example present extension)
*/
Bool
exynosLayerSetDraw(EXYNOSLayer * layer, DrawablePtr pDraw)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, FALSE);

    EXYNOSVideoBuf *pVbuf = NULL;
    PixmapPtr pPixmap;
    EXYNOSPixmapPriv *privPixmap;

    if (pDraw->type == DRAWABLE_WINDOW)
        pPixmap = (pDraw->pScreen->GetWindowPixmap) ((WindowPtr) pDraw);
    else
        pPixmap = (PixmapPtr) pDraw;
    privPixmap = exaGetPixmapDriverPrivate(pPixmap);
    XDBG_RETURN_VAL_IF_FAIL(privPixmap != NULL, FALSE);

    tbm_bo new_bo = privPixmap->bo;

    if (new_bo == NULL) {
        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL(privPixmap->bo != NULL, fail);
        new_bo = privPixmap->bo;
        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);
    }

    pVbuf = exynosLayerGetMatchBuf(layer, new_bo);
    if (pVbuf == NULL) {
        pVbuf = exynosUtilCreateVideoBufferByDraw(pDraw);
        XDBG_RETURN_VAL_IF_FAIL(pVbuf != NULL, FALSE);

        XDBG_GOTO_IF_FAIL(exynosLayerSetBuffer(layer, pVbuf), fail);

        //do unref for videobuf because videobuf is local variable
        exynosUtilVideoBufferUnref(pVbuf);
    }
    else {
        XDBG_DEBUG(MVBUF, "frame buffer(%d) already set on layer(%p)\n",
                   pVbuf->fb_id, layer->lpos);
    }

    layer->pDraw = pDraw;

    return TRUE;
 fail:
    exynosUtilFreeVideoBuffer(pVbuf);
    return FALSE;
}

Bool
exynosLayerSetAsDefault(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, FALSE);
    EXYNOSLayer *lyr = NULL;

    _exynosLayerInitList();

    xorg_list_for_each_entry(lyr, &crtc_layers, link) {
        /* FIXME :: search old default for same crtc and reset it */
        if ( /*lyr->crtc_id == layer->crtc_id && */ lyr->default_layer)
            lyr->default_layer = FALSE;
    }

    layer->default_layer = TRUE;
    return TRUE;
}

EXYNOSLayerPtr
exynosLayerGetDefault(xf86CrtcPtr pCrtc)
{
    EXYNOSLayer *layer = NULL;

//    EXYNOSCrtcPrivPtr pPrivCrtc = (EXYNOSCrtcPrivPtr)pCrtc->driver_private;

    _exynosLayerInitList();

    xorg_list_for_each_entry(layer, &crtc_layers, link) {
        /* FIXME :: def layer can be in each crtc */
        if (layer->default_layer
            /*&& layer->crtc_id == pPrivCrtc->mode_crtc->crtc_id */ )
            return layer;
    }

    return NULL;
}

ScrnInfoPtr
exynosLayerGetScrn(EXYNOSLayer * layer)
{
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);
    return layer->pScrn;
}

Bool
exynosLayerIsUpdateDRI(EXYNOSLayer * layer)
{
    return layer->is_updated_dri;
}

void
exynosLayerUpdateDRI(EXYNOSLayer * layer, Bool is_updated_dri)
{
    layer->is_updated_dri = is_updated_dri;
}

void
exynosLayerVBlankEventHandler(unsigned int frame, unsigned int tv_sec,
                              unsigned int tv_usec, void *event_data)
{
    EXYNOSLayer *layer = NULL, *layer_next = NULL;
    intptr_t pipe = (intptr_t) event_data;

    XDBG_RETURN_IF_FAIL(pipe < LAYER_OUTPUT_MAX);

    _exynosLayerInitList();

    wait_vblank[pipe] = FALSE;

    XDBG_DEBUG(MLYR, "frame(%d), tv_sec(%d), tv_usec(%d) \n", frame, tv_sec,
               tv_usec);

    xorg_list_for_each_entry_safe(layer, layer_next, &crtc_layers, link) {
        intptr_t crtc_pipe =
            exynosDisplayCrtcPipe(layer->pScrn, _GetCrtcID(layer));

        if (!layer->enable_vblank || !layer->wait_vblank)
            continue;

        if (crtc_pipe != pipe)
            continue;

        layer->wait_vblank = FALSE;

        if (VBUF_IS_VALID(layer->wait_vbuf)) {
            if (layer->showing_vbuf && VBUF_IS_VALID(layer->showing_vbuf)) {
                layer->showing_vbuf->showing = FALSE;
                exynosUtilVideoBufferUnref(layer->showing_vbuf);
            }

            layer->showing_vbuf = layer->wait_vbuf;
            layer->wait_vbuf = NULL;

            if (layer->pending_vbuf && VBUF_IS_VALID(layer->pending_vbuf)) {
                intptr_t fb_id;

                layer->wait_vbuf = layer->pending_vbuf;
                layer->pending_vbuf = NULL;

                fb_id =
                    exynosPlaneGetBuffer(layer->plane_id, NULL,
                                         layer->wait_vbuf);
                if (fb_id == 0) {
                    fb_id =
                        exynosPlaneAddBuffer(layer->plane_id, layer->wait_vbuf);
                    XDBG_RETURN_IF_FAIL(fb_id > 0);

                    layer->fb_id = layer->wait_vbuf->fb_id;
                }

                if (!exynosPlaneAttach(layer->plane_id, 0, layer->wait_vbuf))
                    continue;

                if (exynosLayerIsVisible(layer) && !layer->freeze_update)
                    _exynosLayerShowInternal(layer, TRUE);

                _exynosLayerWatchVblank(layer);
            }

            EXYNOSPtr pExynos = EXYNOSPTR(layer->pScrn);

            if (pExynos->pVideoPriv->video_fps)
                _countFps(layer);

            XDBG_TRACE(MLYR,
                       "layer(%p) fb_id(%d) now showing frame(%d) (%ld,%ld,%ld) => crtc(%d) lpos(%d). \n",
                       layer, layer->fb_id, frame, VSTMAP(layer->pending_vbuf),
                       VSTMAP(layer->wait_vbuf), VSTMAP(layer->showing_vbuf),
                       _GetCrtcID(layer), layer->lpos);

            _exynosLayerNotify(layer, LAYER_VBLANK,
                               (void *) layer->showing_vbuf);

            //call the Vblank signal when vbuf has been showing;
            if (layer->showing_vbuf->vblank_handler) {
                layer->showing_vbuf->vblank_handler(frame, tv_sec, tv_usec,
                                                    layer->showing_vbuf->
                                                    vblank_user_data, FALSE);
                layer->showing_vbuf->vblank_handler = NULL;
                layer->showing_vbuf->vblank_user_data = NULL;
            }
        }
    }
}
