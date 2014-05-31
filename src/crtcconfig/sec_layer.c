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

#include "sec.h"
#include "sec_util.h"
#include "sec_crtc.h"
#include "sec_output.h"
#include "sec_plane.h"
#include "sec_layer.h"
#include "sec_video_fourcc.h"
#include "sec_video_tvout.h"
#include "sec_video_virtual.h"

#include <exynos_drm.h>

//#define DEBUG_REFCNT

#ifdef DEBUG_REFCNT
#define SEC_LAYER_PRINT_REFCNT(b) \
            XDBG_TRACE(MLYR, "layer(%p) ref_cnt(%d) \n", b, b->ref_cnt)
#else
#define SEC_LAYER_PRINT_REFCNT(b)
#endif

typedef struct _NotifyFuncData
{
    NotifyFunc  func;
    void       *user_data;

    struct xorg_list link;
} NotifyFuncData;

struct _SECLayer
{
    ScrnInfoPtr    pScrn;

    SECLayerOutput output;
    SECLayerPos lpos;

    int         plane_id;
    int         crtc_id;

    /* for buffer */
    int         fb_id;

    int         offset_x;
    int         offset_y;

    xRectangle *src;
    xRectangle *dst;

    SECVideoBuf *vbuf;
    Bool         visible;

    /* vblank */
    Bool        enable_vblank;
    Bool        wait_vblank;
    SECVideoBuf *wait_vbuf;
    SECVideoBuf *pending_vbuf;
    SECVideoBuf *showing_vbuf;

    struct xorg_list noti_data;
    struct xorg_list link;

    Bool onoff;
    int ref_cnt;
    Bool freeze_update;

    /* count */
    unsigned int put_counts;
    OsTimerPtr timer;
};

static Bool crtc_layers_init;
static struct xorg_list crtc_layers;
static Bool wait_vblank[LAYER_OUTPUT_MAX];

#define LAYER_VBLANK_FLAG 0xFFFF

static CARD32
_countPrint (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECLayer *layer = (SECLayer*)arg;

    if (layer->timer)
    {
        TimerFree (layer->timer);
        layer->timer = NULL;
    }

    XDBG_DEBUG (MEXA, "crtc(%d) pos(%d) : %d fps. \n",
            layer->crtc_id, layer->lpos, layer->put_counts);

    layer->put_counts = 0;

    return 0;
}

static void
_countFps (SECLayer *layer)
{
    layer->put_counts++;

    if (layer->timer)
        return;

    layer->timer = TimerSet (NULL, 0, 1000, _countPrint, layer);
}

static void
_secLayerInitList (void)
{
    if (!crtc_layers_init)
    {
        xorg_list_init (&crtc_layers);
        crtc_layers_init = TRUE;
    }
}

static void
_secLayerNotify (SECLayer *layer, int type, void *type_data)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    xorg_list_for_each_entry_safe (data, data_next, &layer->noti_data, link)
    {
        if (data->func)
            data->func (layer, type, type_data, data->user_data);
    }
}

static int
_GetCrtcIdForOutput (ScrnInfoPtr pScrn, SECLayerOutput output)
{
    SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
    SECOutputPrivPtr pOutputPriv = NULL;
    int crtc_id = 0;

    switch (output)
    {
    case LAYER_OUTPUT_LCD:
        pOutputPriv = secOutputGetPrivateForConnType (pScrn, DRM_MODE_CONNECTOR_LVDS);
        if (!pOutputPriv)
            pOutputPriv = secOutputGetPrivateForConnType (pScrn, DRM_MODE_CONNECTOR_Unknown);
        if (pOutputPriv && pOutputPriv->mode_encoder)
            crtc_id = pOutputPriv->mode_encoder->crtc_id;
        break;
    case LAYER_OUTPUT_EXT:
        if (pSecMode->conn_mode == DISPLAY_CONN_MODE_HDMI)
        {
            pOutputPriv = secOutputGetPrivateForConnType (pScrn, DRM_MODE_CONNECTOR_HDMIA);
            if (!pOutputPriv)
                pOutputPriv = secOutputGetPrivateForConnType (pScrn, DRM_MODE_CONNECTOR_HDMIB);
            if (pOutputPriv && pOutputPriv->mode_encoder)
                crtc_id = pOutputPriv->mode_encoder->crtc_id;
        }
        else if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        {
            pOutputPriv = secOutputGetPrivateForConnType (pScrn, DRM_MODE_CONNECTOR_VIRTUAL);
            if (pOutputPriv && pOutputPriv->mode_encoder)
                crtc_id = pOutputPriv->mode_encoder->crtc_id;
        }
        break;
    default:
        break;
    }

    XDBG_DEBUG (MLYR, "crtc(%d) for output(%d) \n", crtc_id, output);

    if (crtc_id == 0)
        XDBG_ERROR (MLYR, "no crtc for output(%d) \n", output);

    return crtc_id;
}

static int
_GetCrtcID (SECLayer *layer)
{
    if (layer->crtc_id > 0)
        return layer->crtc_id;

    layer->crtc_id = _GetCrtcIdForOutput (layer->pScrn, layer->output);

    XDBG_RETURN_VAL_IF_FAIL (layer->crtc_id > 0, 0);

    return layer->crtc_id;
}

static int
_secLayerGetPlanePos (SECLayer *layer, SECLayerPos lpos)
{
    if (layer->output == LAYER_OUTPUT_LCD)
    {
        XDBG_DEBUG (MLYR, "lpos(%d) => ppos(%d) (1)\n", lpos, PLANE_POS_3 + lpos);
        return PLANE_POS_3 + lpos;
    }
    else if (layer->output == LAYER_OUTPUT_EXT)
    {
        if (lpos == -1)
        {
            XDBG_DEBUG (MLYR, "lpos(%d) => ppos(%d) (2)\n", lpos, PLANE_POS_2);
            return PLANE_POS_2;
        }
        else
        {
            XDBG_DEBUG (MLYR, "lpos(%d) => ppos(%d) (3)\n", lpos, PLANE_POS_0 + lpos);
            return PLANE_POS_0 + lpos;
        }
    }
    else
    {
        XDBG_NEVER_GET_HERE (MLYR);
    }

    return -1;
}

static void
_secLayerDestroy (SECLayer *layer)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL (layer != NULL);

    xorg_list_del (&layer->link);

    if (layer->src)
        free (layer->src);
    if (layer->dst)
        free (layer->dst);

    if (layer->wait_vbuf)
        secUtilVideoBufferUnref (layer->wait_vbuf);
    if (layer->pending_vbuf)
        secUtilVideoBufferUnref (layer->pending_vbuf);
    if (layer->showing_vbuf)
        secUtilVideoBufferUnref (layer->showing_vbuf);
    if (layer->vbuf)
    {
        secUtilVideoBufferUnref (layer->vbuf);
        layer->vbuf = NULL;
    }

    XDBG_TRACE (MLYR, "layer(%p) destroyed. \n", layer);
    SEC_LAYER_PRINT_REFCNT (layer);

    _secLayerNotify (layer, LAYER_DESTROYED, NULL);

    xorg_list_for_each_entry_safe (data, data_next, &layer->noti_data, link)
    {
        xorg_list_del (&data->link);
        free (data);
    }

    if (layer->plane_id > 0)
        secPlaneFreeId (layer->plane_id);

    free (layer);
}

static void
_secLayerWatchVblank (SECLayer *layer)
{
    CARD64 ust, msc, target_msc;
    int pipe, flip = 1;
    SECPtr pSec = SECPTR (layer->pScrn);

    /* if lcd is off, do not request vblank information */
    if (pSec->isLcdOff)
        return;

    pipe = secDisplayCrtcPipe (layer->pScrn, _GetCrtcID (layer));

    layer->wait_vblank = TRUE;

    if (wait_vblank[pipe])
        return;

    wait_vblank[pipe] = TRUE;

    if (!secDisplayGetCurMSC (layer->pScrn, pipe, &ust, &msc))
        XDBG_WARNING (MLYR, "fail to get current_msc.\n");

    target_msc = msc + 1;

    XDBG_TRACE (MLYR, "layer(%p) wait vblank : cur(%lld) target(%lld). \n",
                layer, msc, target_msc);

    if (!secDisplayVBlank (layer->pScrn, pipe, &target_msc, flip, VBLANK_INFO_PLANE, (void*)pipe))
        XDBG_WARNING (MLYR, "fail to Vblank.\n");
}

static Bool
_secLayerShowInternal (SECLayer *layer, Bool need_update)
{
    int crtc_id, plane_pos;

    XDBG_RETURN_VAL_IF_FAIL (layer->fb_id > 0, FALSE);

    crtc_id = _GetCrtcID (layer);
    plane_pos = _secLayerGetPlanePos (layer, layer->lpos);

    if (!secPlaneShow (layer->plane_id, crtc_id,
                       layer->src->x, layer->src->y,
                       layer->src->width, layer->src->height,
                       layer->offset_x + layer->dst->x,
                       layer->offset_y + layer->dst->y,
                       layer->dst->width, layer->dst->height,
                       plane_pos, need_update))
        return FALSE;

    return TRUE;
}

static void
_secLayerGetBufferID (SECLayer *layer, SECVideoBuf *vbuf)
{
    SECModePtr pSecMode;
    unsigned int drmfmt;
    unsigned int handles[4] = {0,};
    unsigned int pitches[4] = {0,};
    unsigned int offsets[4] = {0,};
    int i;

    if (vbuf->fb_id > 0)
        return;

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;
    drmfmt = secUtilGetDrmFormat (vbuf->id);

    for (i = 0 ; i < PLANAR_CNT; i++)
    {
        handles[i] = (unsigned int)vbuf->handles[i];
        pitches[i] = (unsigned int)vbuf->pitches[i];
        offsets[i] = (unsigned int)vbuf->offsets[i];
    }

    if (drmModeAddFB2 (pSecMode->fd, vbuf->width, vbuf->height, drmfmt,
                       handles, pitches, offsets, &vbuf->fb_id, 0))
    {
        XDBG_ERRNO (MLYR, "drmModeAddFB2 failed. handles(%d %d %d) pitches(%d %d %d) offsets(%d %d %d) '%c%c%c%c'\n",
                    handles[0], handles[1], handles[2],
                    pitches[0], pitches[1], pitches[2],
                    offsets[0], offsets[1], offsets[2],
                    FOURCC_STR (drmfmt));
    }

    XDBG_DEBUG (MVBUF, "layer(%p) vbuf(%ld) fb_id(%d) added. \n", layer, vbuf->stamp, vbuf->fb_id);
}

Bool
secLayerSupport (ScrnInfoPtr pScrn, SECLayerOutput output, SECLayerPos lpos, unsigned int id)
{
    SECModePtr pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (pScrn != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (output < LAYER_OUTPUT_MAX, FALSE);

    pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;

    if (output == LAYER_OUTPUT_EXT && lpos == LAYER_LOWER1)
    {
        if (pSecMode->conn_mode == DISPLAY_CONN_MODE_HDMI)
        {
            if (id == FOURCC_SN12 || id == FOURCC_ST12)
                return TRUE;
            else
                return FALSE;
        }
        else if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        {
            if (id == FOURCC_SN12 || id == FOURCC_RGB32)
                return TRUE;
            else
                return FALSE;
        }
    }

    return (id == FOURCC_RGB32 || id == FOURCC_SR32) ? TRUE : FALSE;
}

SECLayer*
secLayerFind (SECLayerOutput output, SECLayerPos lpos)
{
    SECLayer *layer = NULL, *layer_next = NULL;

    XDBG_RETURN_VAL_IF_FAIL (output < LAYER_OUTPUT_MAX, NULL);

    _secLayerInitList ();

    xorg_list_for_each_entry_safe (layer, layer_next, &crtc_layers, link)
    {
        if (layer->output == output && layer->lpos == lpos)
            return layer;
    }

    return NULL;
}

void
secLayerDestroyAll (void)
{
    SECLayer *layer = NULL, *layer_next = NULL;

    _secLayerInitList ();

    xorg_list_for_each_entry_safe (layer, layer_next, &crtc_layers, link)
    {
        _secLayerDestroy (layer);
    }
}

void
secLayerShowAll (ScrnInfoPtr pScrn, SECLayerOutput output)
{
    int crtc_id = _GetCrtcIdForOutput (pScrn, output);

    secPlaneShowAll (crtc_id);
}

SECLayer*
secLayerCreate (ScrnInfoPtr pScrn, SECLayerOutput output, SECLayerPos lpos)
{
    SECLayer* layer;

    XDBG_RETURN_VAL_IF_FAIL (pScrn != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (output < LAYER_OUTPUT_MAX, NULL);
    XDBG_RETURN_VAL_IF_FAIL (lpos != LAYER_DEFAULT, NULL);

    layer = secLayerFind (output, lpos);
    if (layer)
    {
        XDBG_ERROR (MLYR, "layer(%p) already is at output(%d) lpos(%d). \n",
                    layer, output, lpos);

        return NULL;
    }

    layer = calloc (sizeof (SECLayer), 1);
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, NULL);

    layer->pScrn = pScrn;
    layer->output = output;
    layer->lpos = lpos;

    layer->plane_id = secPlaneGetID ();
    if (layer->plane_id < 0)
    {
        free (layer);
        return NULL;
    }

    layer->ref_cnt = 1;

    xorg_list_init (&layer->noti_data);

    _secLayerInitList ();

    xorg_list_add(&layer->link, &crtc_layers);

    XDBG_TRACE (MLYR, "layer(%p) output(%d) lpos(%d) created. \n", layer, output, lpos);
    SEC_LAYER_PRINT_REFCNT (layer);

    return layer;
}

SECLayer*
secLayerRef (SECLayer* layer)
{
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, NULL);

    layer->ref_cnt++;

    SEC_LAYER_PRINT_REFCNT (layer);

    return layer;
}

void
secLayerUnref (SECLayer* layer)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    layer->ref_cnt--;

    SEC_LAYER_PRINT_REFCNT (layer);

    if (layer->ref_cnt == 0)
    {
        secLayerHide (layer);
        _secLayerDestroy (layer);
    }
}

void
secLayerAddNotifyFunc (SECLayer* layer, NotifyFunc func, void *user_data)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL (layer != NULL);
    XDBG_RETURN_IF_FAIL (func != NULL);

    xorg_list_for_each_entry_safe (data, data_next, &layer->noti_data, link)
    {
        if (data->func == func && data->user_data == user_data)
            return;
    }

    data = calloc (sizeof (NotifyFuncData), 1);
    XDBG_RETURN_IF_FAIL (data != NULL);

    data->func      = func;
    data->user_data = user_data;

    xorg_list_add (&data->link, &layer->noti_data);
}

void
secLayerRemoveNotifyFunc (SECLayer* layer, NotifyFunc func)
{
    NotifyFuncData *data = NULL, *data_next = NULL;

    XDBG_RETURN_IF_FAIL (layer != NULL);
    XDBG_RETURN_IF_FAIL (func != NULL);

    xorg_list_for_each_entry_safe (data, data_next, &layer->noti_data, link)
    {
        if (data->func == func)
        {
            xorg_list_del (&data->link);
            free (data);
        }
    }
}

Bool
secLayerIsVisible (SECLayer *layer)
{
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);

    return layer->visible;
}

void
secLayerShow (SECLayer *layer)
{
    SECModePtr pSecMode;

    XDBG_RETURN_IF_FAIL (layer != NULL);
    XDBG_RETURN_IF_FAIL (layer->fb_id > 0);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (layer->visible)
        return;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
    {
        layer->visible = TRUE;
        XDBG_TRACE (MLYR, "layer(%p) shown. \n", layer);
        return;
    }

    if (!_secLayerShowInternal (layer, FALSE))
        return;

    if (layer->enable_vblank)
        _secLayerWatchVblank (layer);

    layer->visible = TRUE;

    XDBG_TRACE (MLYR, "layer(%p) shown. \n", layer);

    _secLayerNotify (layer, LAYER_SHOWN, (void*)layer->fb_id);
}

void
secLayerHide (SECLayer *layer)
{
    SECModePtr pSecMode;

    XDBG_RETURN_IF_FAIL (layer != NULL);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (!layer->visible || layer->ref_cnt > 1)
        return;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
    {
        layer->visible = FALSE;
        XDBG_TRACE (MLYR, "layer(%p) hidden. \n", layer);
        return;
    }

    if (!secPlaneHide (layer->plane_id))
        return;

    if (layer->wait_vbuf && VBUF_IS_VALID (layer->wait_vbuf))
    {
        layer->wait_vbuf->showing = FALSE;
        XDBG_DEBUG (MVBUF, "layer(%p) <-- %s (%ld,%d,%d) \n", layer,
                    (layer->output==LAYER_OUTPUT_LCD)?"LCD":"TV",
                    layer->wait_vbuf->stamp, VBUF_IS_CONVERTING (layer->wait_vbuf),
                    layer->wait_vbuf->showing);
        secUtilVideoBufferUnref (layer->wait_vbuf);
    }

    if (layer->pending_vbuf && VBUF_IS_VALID (layer->pending_vbuf))
    {
        layer->pending_vbuf->showing = FALSE;
        secUtilVideoBufferUnref (layer->pending_vbuf);
    }

    if (layer->showing_vbuf && VBUF_IS_VALID (layer->showing_vbuf))
    {
        layer->showing_vbuf->showing = FALSE;
        XDBG_DEBUG (MVBUF, "layer(%p) <-- %s (%ld,%d,%d) \n", layer,
                    (layer->output==LAYER_OUTPUT_LCD)?"LCD":"TV",
                    layer->showing_vbuf->stamp, VBUF_IS_CONVERTING (layer->showing_vbuf),
                    layer->showing_vbuf->showing);
        secUtilVideoBufferUnref (layer->showing_vbuf);
    }

    layer->showing_vbuf = NULL;
    layer->pending_vbuf = NULL;
    layer->wait_vbuf = NULL;
    layer->wait_vblank = FALSE;
    layer->visible = FALSE;
    layer->crtc_id = 0;

    XDBG_TRACE (MLYR, "layer(%p) hidden. \n", layer);

    _secLayerNotify (layer, LAYER_HIDDEN, (void*)layer->fb_id);
}

void
secLayerFreezeUpdate (SECLayer *layer, Bool enable)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    layer->freeze_update = enable;

    XDBG_TRACE (MLYR, "layer(%p) freeze %d. \n", layer, enable);

    if (layer->plane_id > 0)
        secPlaneFreezeUpdate (layer->plane_id, enable);
}

void
secLayerUpdate (SECLayer *layer)
{
    SECModePtr pSecMode;

    XDBG_RETURN_IF_FAIL (layer != NULL);
    XDBG_RETURN_IF_FAIL (layer->fb_id > 0);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (!layer->visible)
        return;

    xf86CrtcConfigPtr pCrtcConfig = XF86_CRTC_CONFIG_PTR (layer->pScrn);
    SECCrtcPrivPtr pCrtcPriv = NULL;
    int c;

    for (c = 0; c < pCrtcConfig->num_crtc; c++)
    {
        xf86CrtcPtr pCrtc = pCrtcConfig->crtc[c];
        SECCrtcPrivPtr pTemp =  pCrtc->driver_private;
        if (pTemp->mode_crtc && pTemp->mode_crtc->crtc_id == layer->crtc_id)
        {
            pCrtcPriv = pTemp;
            break;
        }
    }

    if (!pCrtcPriv || !pCrtcPriv->bAccessibility)
        return;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return;

    if (!_secLayerShowInternal (layer, TRUE))
        return;
}

void
secLayerTurn (SECLayer *layer, Bool onoff, Bool user)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    secPlaneTrun (layer->plane_id, onoff, user);
}

Bool
secLayerTurnStatus (SECLayer *layer)
{
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);

    return secPlaneTrunStatus (layer->plane_id);
}

void
secLayerEnableVBlank (SECLayer *layer, Bool enable)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    layer->enable_vblank = (enable) ? TRUE : FALSE;
}

Bool
secLayerSetOffset (SECLayer *layer, int x, int y)
{
    SECModePtr pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (layer->offset_x == x && layer->offset_y == y)
        return TRUE;

    /* display controller restriction. x+width=2's mutiple */
    XDBG_TRACE (MLYR, "layer(%p) offset(%d,%d => %d,%d).\n",
                layer, x, y, x & (~0x1), y);
    layer->offset_x = x & (~0x1);
    layer->offset_y = y;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return TRUE;

    if (secLayerIsVisible (layer) && !layer->freeze_update)
    {
        int crtc_id = _GetCrtcID (layer);
        int plane_pos = _secLayerGetPlanePos (layer, layer->lpos);

        if (!secPlaneShow (layer->plane_id, crtc_id,
                           layer->src->x, layer->src->y,
                           layer->src->width, layer->src->height,
                           layer->offset_x + layer->dst->x,
                           layer->offset_y + layer->dst->y,
                           layer->dst->width, layer->dst->height,
                           plane_pos, FALSE))
            return FALSE;
    }

    return TRUE;
}

void
secLayerGetOffset (SECLayer *layer, int *x, int *y)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    if (x)
        *x = layer->offset_x;
    if (y)
        *y = layer->offset_y;
}

Bool
secLayerSetPos (SECLayer *layer, SECLayerPos lpos)
{
    SECModePtr pSecMode;
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (lpos >= LAYER_NONE && lpos < LAYER_MAX, FALSE);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
    {
        layer->lpos = lpos;
        return TRUE;
    }


    if (layer->lpos == lpos)
        return TRUE;

    if (secLayerFind (layer->output, lpos))
        return FALSE;

    if (secLayerIsVisible (layer) && !layer->freeze_update)
    {
        if (lpos == LAYER_NONE)
        {
            if (!secPlaneHide (layer->plane_id))
                return FALSE;

            layer->visible = FALSE;
            layer->crtc_id = 0;
        }
        else
        {
            int crtc_id = _GetCrtcID (layer);
            int plane_pos = _secLayerGetPlanePos (layer, lpos);

            if (!secPlaneShow (layer->plane_id, crtc_id,
                               layer->src->x, layer->src->y,
                               layer->src->width, layer->src->height,
                               layer->offset_x + layer->dst->x,
                               layer->offset_y + layer->dst->y,
                               layer->dst->width, layer->dst->height,
                               plane_pos, FALSE))
                return FALSE;
        }
    }

    XDBG_TRACE (MLYR, "layer(%p) lpos(%d). \n", layer, lpos);

    layer->lpos = lpos;

    return TRUE;
}

Bool
secLayerSwapPos (SECLayer *layer1, SECLayer *layer2)
{
    SECLayer *lower, *upper;
    SECLayerPos upper_lpos, lower_lpos;

    XDBG_RETURN_VAL_IF_FAIL (layer1 != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (layer2 != NULL, FALSE);

    XDBG_TRACE (MLYR, "layer1(%p) layer2(%p). \n", layer1, layer2);

    lower = (layer2->lpos < layer1->lpos) ? layer2 : layer1;
    upper = (layer2->lpos < layer1->lpos) ? layer1 : layer2;

    upper_lpos = upper->lpos;
    lower_lpos = lower->lpos;

    secLayerSetPos (upper, LAYER_NONE);
    secLayerSetPos (lower, upper_lpos);
    secLayerSetPos (upper, lower_lpos);

    return TRUE;
}

SECLayerPos
secLayerGetPos (SECLayer *layer)
{
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, 0);

    return layer->lpos;
}

Bool
secLayerSetRect (SECLayer *layer, xRectangle *src, xRectangle *dst)
{
    SECModePtr pSecMode;

    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst != NULL, FALSE);

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (!layer->src)
        layer->src = calloc (sizeof (xRectangle), 1);

    XDBG_RETURN_VAL_IF_FAIL (layer->src != NULL, FALSE);

    if (!layer->dst)
        layer->dst = calloc (sizeof (xRectangle), 1);

    XDBG_RETURN_VAL_IF_FAIL (layer->dst != NULL, FALSE);

    if (!memcmp (layer->src, src, sizeof (xRectangle)) &&
        !memcmp (layer->dst, dst, sizeof (xRectangle)))
        return TRUE;

    *layer->src = *src;
    *layer->dst = *dst;

    XDBG_TRACE (MLYR, "layer(%p) src(%d,%d %dx%d) dst(%d,%d %dx%d). \n",
                layer, src->x, src->y, src->width, src->height,
                dst->x, dst->y, dst->width, dst->height);
    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
        return TRUE;

    if (layer->pending_vbuf && VBUF_IS_VALID (layer->pending_vbuf))
    {
        layer->pending_vbuf->showing = FALSE;
        secUtilVideoBufferUnref (layer->pending_vbuf);
        layer->pending_vbuf = NULL;
    }

    if (secLayerIsVisible (layer) && !layer->freeze_update)
    {
        int plane_pos = _secLayerGetPlanePos (layer, layer->lpos);

        if (!secPlaneShow (layer->plane_id, _GetCrtcID (layer),
                           src->x, src->y, src->width, src->height,
                           layer->offset_x + dst->x,
                           layer->offset_y + dst->y,
                           dst->width, dst->height,
                           plane_pos, FALSE))
            return FALSE;
    }

    return TRUE;
}

void
secLayerGetRect (SECLayer *layer, xRectangle *src, xRectangle *dst)
{
    XDBG_RETURN_IF_FAIL (layer != NULL);

    if (src && layer->src)
        *src = *layer->src;

    if (dst && layer->dst)
        *dst = *layer->dst;
}

int
secLayerSetBuffer (SECLayer *layer, SECVideoBuf *vbuf)
{
    SECModePtr pSecMode;
    unsigned int fb_id;

    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, 0);
    XDBG_RETURN_VAL_IF_FAIL (VBUF_IS_VALID (vbuf), 0);

    if (!secLayerSupport (layer->pScrn, layer->output, layer->lpos, vbuf->id))
    {
        XDBG_ERROR (MLYR, "fail : layer(%p) output(%d) lpos(%d) vbuf(%c%c%c%c)\n",
                    layer, layer->output, layer->lpos, FOURCC_STR (vbuf->id));
        return 0;
    }

    pSecMode = (SECModePtr) SECPTR (layer->pScrn)->pSecMode;

    if (layer->output == LAYER_OUTPUT_EXT && pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
    {
        XDBG_RETURN_VAL_IF_FAIL (layer->enable_vblank == FALSE, 0);

        XDBG_TRACE (MLYR, "layer(%p) vbuf('%c%c%c%c', %dx%d, %d,%d %dx%d)\n",
                    layer, FOURCC_STR(vbuf->id), vbuf->width, vbuf->height,
                    vbuf->crop.x, vbuf->crop.y, vbuf->crop.width, vbuf->crop.height);

        if (layer->vbuf)
            secUtilVideoBufferUnref (layer->vbuf);

        layer->vbuf = secUtilVideoBufferRef (vbuf);
        layer->fb_id = 1;

        _secLayerNotify (layer, LAYER_BUF_CHANGED, vbuf);

        return layer->fb_id;
    }

    if (layer->wait_vbuf && layer->pending_vbuf)
    {
        XDBG_TRACE (MLYR, "pending_vbuf(%ld) exists.\n", layer->pending_vbuf->stamp);
        return 0;
    }

    _secLayerGetBufferID (layer, vbuf);
    XDBG_RETURN_VAL_IF_FAIL (vbuf->fb_id > 0, 0);

    if (layer->wait_vbuf && !layer->pending_vbuf)
    {
        layer->pending_vbuf = secUtilVideoBufferRef (vbuf);
        layer->pending_vbuf->showing = TRUE;
        XDBG_TRACE (MLYR, "pending vbuf(%ld).\n", layer->pending_vbuf->stamp);
        return vbuf->fb_id;
    }

    fb_id = secPlaneGetBuffer (layer->plane_id, NULL, vbuf);
    if (fb_id == 0)
    {
        fb_id = secPlaneAddBuffer (layer->plane_id, vbuf);
        XDBG_RETURN_VAL_IF_FAIL (fb_id > 0, 0);

        layer->fb_id = vbuf->fb_id;
    }

    if (vbuf->fb_id != fb_id)
        XDBG_WARNING (MLYR, "fb_id (%d != %d) \n", vbuf->fb_id, fb_id);

    layer->fb_id = fb_id;
    if (!secPlaneAttach (layer->plane_id, fb_id))
        return 0;

    if (secLayerIsVisible (layer) && !layer->freeze_update)
        if (!_secLayerShowInternal (layer, TRUE))
            return 0;

    if (layer->enable_vblank)
    {
        XDBG_RETURN_VAL_IF_FAIL (layer->wait_vbuf == NULL, 0);

        layer->wait_vbuf = secUtilVideoBufferRef (vbuf);
        layer->wait_vbuf->showing = TRUE;
        XDBG_DEBUG (MVBUF, "layer(%p) --> %s (%ld,%d,%d) \n", layer,
                    (layer->output==LAYER_OUTPUT_LCD)?"LCD":"TV",
                    layer->wait_vbuf->stamp,
                    VBUF_IS_CONVERTING (layer->wait_vbuf),
                    layer->wait_vbuf->showing);

        if (secLayerIsVisible (layer))
        {
            XDBG_TRACE (MLYR, "layer(%p) fb_id(%d) attached. \n", layer, fb_id);
            _secLayerWatchVblank (layer);
        }
    }

    if (layer->vbuf)
        secUtilVideoBufferUnref (layer->vbuf);
    layer->vbuf = secUtilVideoBufferRef (vbuf);

    _secLayerNotify (layer, LAYER_BUF_CHANGED, vbuf);

    return fb_id;
}

SECVideoBuf*
secLayerGetBuffer (SECLayer *layer)
{
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, NULL);

    if (layer->showing_vbuf && layer->dst && layer->visible)
        return layer->showing_vbuf;
    else if (layer->vbuf)
        return layer->vbuf;

    return NULL;
}

void
secLayerVBlankEventHandler (unsigned int frame, unsigned int tv_sec,
                            unsigned int tv_usec, void *event_data)
{
    SECLayer *layer = NULL, *layer_next = NULL;
    int pipe = (int)event_data;

    XDBG_RETURN_IF_FAIL (pipe < LAYER_OUTPUT_MAX);

    _secLayerInitList ();

    wait_vblank[pipe] = FALSE;

    XDBG_DEBUG (MLYR, "frame(%d), tv_sec(%d), tv_usec(%d) \n", frame, tv_sec, tv_usec);

    xorg_list_for_each_entry_safe (layer, layer_next, &crtc_layers, link)
    {
        int crtc_pipe = secDisplayCrtcPipe (layer->pScrn, _GetCrtcID (layer));

        if (!layer->enable_vblank || !layer->wait_vblank)
            continue;

        if (crtc_pipe != pipe)
            continue;

        layer->wait_vblank = FALSE;

        if (VBUF_IS_VALID (layer->wait_vbuf))
        {
            if (layer->showing_vbuf && VBUF_IS_VALID (layer->showing_vbuf))
            {
                layer->showing_vbuf->showing = FALSE;
                secUtilVideoBufferUnref (layer->showing_vbuf);
            }

            layer->showing_vbuf = layer->wait_vbuf;
            layer->wait_vbuf = NULL;

            if (layer->pending_vbuf && VBUF_IS_VALID (layer->pending_vbuf))
            {
                int fb_id;

                layer->wait_vbuf = layer->pending_vbuf;
                layer->pending_vbuf = NULL;

                fb_id = secPlaneGetBuffer (layer->plane_id, NULL, layer->wait_vbuf);
                if (fb_id == 0)
                {
                    fb_id = secPlaneAddBuffer (layer->plane_id, layer->wait_vbuf);
                    XDBG_RETURN_IF_FAIL (fb_id > 0);

                    layer->fb_id = layer->wait_vbuf->fb_id;
                }

                if (!secPlaneAttach (layer->plane_id, layer->wait_vbuf->fb_id))
                    continue;

                if (secLayerIsVisible (layer) && !layer->freeze_update)
                    _secLayerShowInternal (layer, TRUE);

                _secLayerWatchVblank (layer);
            }

            SECPtr pSec = SECPTR (layer->pScrn);
            if (pSec->pVideoPriv->video_fps)
                _countFps (layer);

            XDBG_TRACE (MLYR, "layer(%p) fb_id(%d) now showing frame(%d) (%ld,%ld,%ld) => crtc(%d) pos(%d). \n",
                        layer, layer->fb_id, frame,
                        VSTMAP(layer->pending_vbuf), VSTMAP(layer->wait_vbuf), VSTMAP(layer->showing_vbuf),
                        _GetCrtcID (layer), layer->lpos);

            _secLayerNotify (layer, LAYER_VBLANK, (void*)layer->showing_vbuf);
        }
    }
}
