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

#include <pixman.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>

#include "sec.h"
#include "sec_util.h"
#include "sec_wb.h"
#include "sec_crtc.h"
#include "sec_converter.h"
#include "sec_output.h"
#include "sec_video.h"
#include "sec_video_fourcc.h"
#include "sec_video_virtual.h"
#include "sec_video_tvout.h"
#include "sec_display.h"
#include "sec_xberc.h"

#include "xv_types.h"

#define DEV_INDEX   2
#define BUF_NUM_EXTERNAL 5
#define BUF_NUM_STREAM  3

enum
{
    CAPTURE_MODE_NONE,
    CAPTURE_MODE_STILL,
    CAPTURE_MODE_STREAM,
    CAPTURE_MODE_MAX,
};

enum
{
    DISPLAY_LCD,
    DISPLAY_EXTERNAL,
};

enum
{
    DATA_TYPE_UI,
    DATA_TYPE_VIDEO,
    DATA_TYPE_MAX,
};

static unsigned int support_formats[] =
{
    FOURCC_RGB32,
    FOURCC_ST12,
    FOURCC_SN12,
};

static XF86VideoEncodingRec dummy_encoding[] =
{
    { 0, "XV_IMAGE", -1, -1, { 1, 1 } },
    { 1, "XV_IMAGE", 2560, 2560, { 1, 1 } },
};

static XF86ImageRec images[] =
{
    XVIMAGE_RGB32,
    XVIMAGE_SN12,
    XVIMAGE_ST12,
};

static XF86VideoFormatRec formats[] =
{
    { 16, TrueColor },
    { 24, TrueColor },
    { 32, TrueColor },
};

static XF86AttributeRec attributes[] =
{
    { 0, 0, 0x7fffffff, "_USER_WM_PORT_ATTRIBUTE_FORMAT" },
    { 0, 0, CAPTURE_MODE_MAX, "_USER_WM_PORT_ATTRIBUTE_CAPTURE" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_DISPLAY" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_ROTATE_OFF" },
    { 0, 0, DATA_TYPE_MAX, "_USER_WM_PORT_ATTRIBUTE_DATA_TYPE" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_SECURE" },
    { 0, 0, 0x7fffffff, "_USER_WM_PORT_ATTRIBUTE_RETURN_BUFFER" },
};

typedef enum
{
    PAA_MIN,
    PAA_FORMAT,
    PAA_CAPTURE,
    PAA_DISPLAY,
    PAA_ROTATE_OFF,
    PAA_DATA_TYPE,
    PAA_SECURE,
    PAA_RETBUF,
    PAA_MAX
} SECPortAttrAtom;

static struct
{
    SECPortAttrAtom  paa;
    const char      *name;
    Atom             atom;
} atoms[] =
{
    { PAA_FORMAT, "_USER_WM_PORT_ATTRIBUTE_FORMAT", None },
    { PAA_CAPTURE, "_USER_WM_PORT_ATTRIBUTE_CAPTURE", None },
    { PAA_DISPLAY, "_USER_WM_PORT_ATTRIBUTE_DISPLAY", None },
    { PAA_ROTATE_OFF, "_USER_WM_PORT_ATTRIBUTE_ROTATE_OFF", None },
    { PAA_DATA_TYPE, "_USER_WM_PORT_ATTRIBUTE_DATA_TYPE", None },
    { PAA_SECURE, "_USER_WM_PORT_ATTRIBUTE_SECURE", None },
    { PAA_RETBUF, "_USER_WM_PORT_ATTRIBUTE_RETURN_BUFFER", None },
};

typedef struct _RetBufInfo
{
    SECVideoBuf *vbuf;
    int type;
    struct xorg_list link;
} RetBufInfo;

/* SEC port information structure */
typedef struct
{
    /* index */
    int     index;

    /* port attribute */
    int     id;
    int     capture;
    int     display;
    Bool    secure;
    Bool    data_type;
    Bool    rotate_off;

    /* information from outside */
    ScrnInfoPtr pScrn;
    DrawablePtr pDraw;
    RegionPtr   clipBoxes;

    /* writeback */
    SECWb *wb;

    /* video */
    SECCvt     *cvt;

    SECVideoBuf *dstbuf;
    SECVideoBuf **outbuf;
    int          outbuf_num;
    int          outbuf_index;

    struct xorg_list retbuf_info;
    Bool         need_damage;

    OsTimerPtr retire_timer;
    Bool putstill_on;

    unsigned int status;
    CARD32       retire_time;
    CARD32       prev_time;

    struct xorg_list link;
} SECPortPriv, *SECPortPrivPtr;

static RESTYPE event_drawable_type;

typedef struct _SECVideoResource
{
    XID            id;
    RESTYPE        type;
    SECPortPrivPtr pPort;
    ScrnInfoPtr    pScrn;
} SECVideoResource;

#define SEC_MAX_PORT        1

#define NUM_IMAGES        (sizeof(images) / sizeof(images[0]))
#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

static DevPrivateKeyRec video_virtual_port_key;
#define VideoVirtualPortKey (&video_virtual_port_key)
#define GetPortInfo(pDraw) ((SECVideoPortInfo*)dixLookupPrivate(&(pDraw)->devPrivates, VideoVirtualPortKey))

typedef struct _SECVideoPortInfo
{
    ClientPtr client;
    XvPortPtr pp;
} SECVideoPortInfo;

static int (*ddPutStill) (ClientPtr, DrawablePtr, struct _XvPortRec *, GCPtr,
                          INT16, INT16, CARD16, CARD16,
                          INT16, INT16, CARD16, CARD16);

static void SECVirtualVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit);
static void _secVirtualVideoCloseOutBuffer (SECPortPrivPtr pPort);
static void _secVirtualVideoLayerNotifyFunc (SECLayer *layer, int type, void *type_data, void *data);
static void _secVirtualVideoWbDumpFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data);
static void _secVirtualVideoWbStopFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data);
static SECVideoBuf* _secVirtualVideoGetBlackBuffer (SECPortPrivPtr pPort);
static Bool _secVirtualVideoEnsureOutBuffers (ScrnInfoPtr pScrn, SECPortPrivPtr pPort, int id, int width, int height);
static void _secVirtualVideoWbCloseFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data);

static SECVideoPortInfo*
_port_info (DrawablePtr pDraw)
{
    if (!pDraw)
        return NULL;

    if (pDraw->type == DRAWABLE_WINDOW)
        return GetPortInfo ((WindowPtr)pDraw);
    else
       return GetPortInfo ((PixmapPtr)pDraw);
}

static Atom
_secVideoGetPortAtom (SECPortAttrAtom paa)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL (paa > PAA_MIN && paa < PAA_MAX, None);

    for (i = 0; i < NUM_ATOMS; i++)
    {
        if (paa == atoms[i].paa)
        {
            if (atoms[i].atom == None)
                atoms[i].atom = MakeAtom (atoms[i].name,
                                          strlen (atoms[i].name), TRUE);

            return atoms[i].atom;
        }
    }

    XDBG_ERROR (MVA, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static void
_secVirtualVideoSetSecure (SECPortPrivPtr pPort, Bool secure)
{
    SECVideoPortInfo *info;

    secure = (secure > 0) ? TRUE : FALSE;

    if (pPort->secure == secure)
        return;

    pPort->secure = secure;

    XDBG_TRACE (MVA, "secure(%d) \n", secure);

    info = _port_info (pPort->pDraw);
    if (!info || !info->pp)
        return;

    XvdiSendPortNotify (info->pp, _secVideoGetPortAtom (PAA_SECURE), secure);
}

static PixmapPtr
_secVirtualVideoGetPixmap (DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pDraw->pScreen->GetWindowPixmap ((WindowPtr) pDraw);
    else
        return (PixmapPtr) pDraw;
}

static Bool
_secVirtualVideoIsSupport (unsigned int id)
{
    int i;

    for (i = 0; i < sizeof (support_formats) / sizeof (unsigned int); i++)
        if (support_formats[i] == id)
            return TRUE;

    return FALSE;
}

#if 0
static char buffers[1024];

static void
_buffers (SECPortPrivPtr pPort)
{
    RetBufInfo *cur = NULL, *next = NULL;

    CLEAR (buffers);
    xorg_list_for_each_entry_safe (cur, next, &pPort->retbuf_info, link)
    {
        if (cur->vbuf)
            snprintf (buffers, 1024, "%s %d", buffers, cur->vbuf->keys[0]);
    }
}
#endif

static RetBufInfo*
_secVirtualVideoFindReturnBuf (SECPortPrivPtr pPort, unsigned int key)
{
    RetBufInfo *cur = NULL, *next = NULL;

    XDBG_RETURN_VAL_IF_FAIL (pPort->capture != CAPTURE_MODE_STILL, NULL);

    xorg_list_for_each_entry_safe (cur, next, &pPort->retbuf_info, link)
    {
        if (cur->vbuf && cur->vbuf->keys[0] == key)
            return cur;
    }

    return NULL;
}

static Bool
_secVirtualVideoAddReturnBuf (SECPortPrivPtr pPort, SECVideoBuf *vbuf)
{
    RetBufInfo *info;

    XDBG_RETURN_VAL_IF_FAIL (pPort->capture != CAPTURE_MODE_STILL, FALSE);

    info = _secVirtualVideoFindReturnBuf (pPort, vbuf->keys[0]);
    XDBG_RETURN_VAL_IF_FAIL (info == NULL, FALSE);

    info = calloc (1, sizeof (RetBufInfo));
    XDBG_RETURN_VAL_IF_FAIL (info != NULL, FALSE);

    info->vbuf = secUtilVideoBufferRef (vbuf);
    info->vbuf->showing = TRUE;

    XDBG_DEBUG (MVA, "retbuf (%ld,%d,%d,%d) added.\n", vbuf->stamp,
                vbuf->keys[0], vbuf->keys[1], vbuf->keys[2]);

    xorg_list_add (&info->link, &pPort->retbuf_info);

    return TRUE;
}

static void
_secVirtualVideoRemoveReturnBuf (SECPortPrivPtr pPort, RetBufInfo *info)
{
    XDBG_RETURN_IF_FAIL (pPort->capture != CAPTURE_MODE_STILL);
    XDBG_RETURN_IF_FAIL (info != NULL);
    XDBG_RETURN_IF_FAIL (info->vbuf != NULL);

    info->vbuf->showing = FALSE;

    XDBG_DEBUG (MVA, "retbuf (%ld,%d,%d,%d) removed.\n", info->vbuf->stamp,
                info->vbuf->keys[0], info->vbuf->keys[1], info->vbuf->keys[2]);

    if (pPort->wb)
        secWbQueueBuffer (pPort->wb, info->vbuf);

    secUtilVideoBufferUnref (info->vbuf);
    xorg_list_del (&info->link);
    free (info);
}

static void
_secVirtualVideoRemoveReturnBufAll (SECPortPrivPtr pPort)
{
    RetBufInfo *cur = NULL, *next = NULL;

    XDBG_RETURN_IF_FAIL (pPort->capture != CAPTURE_MODE_STILL);

    xorg_list_for_each_entry_safe (cur, next, &pPort->retbuf_info, link)
    {
        _secVirtualVideoRemoveReturnBuf (pPort, cur);
    }
}

static void
_secVirtualVideoDraw (SECPortPrivPtr pPort, SECVideoBuf *buf)
{
    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (!pPort->pDraw)
    {
        XDBG_TRACE (MVA, "drawable gone!\n");
        return;
    }

    XDBG_RETURN_IF_FAIL (pPort->need_damage == TRUE);
    XDBG_GOTO_IF_FAIL (VBUF_IS_VALID (buf), draw_done);

    XDBG_TRACE (MVA, "%c%c%c%c, %dx%d. \n",
                FOURCC_STR (buf->id), buf->width, buf->height);

    if (pPort->id == FOURCC_RGB32)
    {
        PixmapPtr pPixmap = _secVirtualVideoGetPixmap (pPort->pDraw);
        tbm_bo_handle bo_handle;

        XDBG_GOTO_IF_FAIL (buf->secure == FALSE, draw_done);

        if (pPort->pDraw->width != buf->width || pPort->pDraw->height != buf->height)
        {
            XDBG_ERROR (MVA, "not matched. (%dx%d != %dx%d) \n",
                        pPort->pDraw->width, pPort->pDraw->height,
                        buf->width, buf->height);
            goto draw_done;
        }

        bo_handle = tbm_bo_map (buf->bo[0], TBM_DEVICE_CPU, TBM_OPTION_READ);
        XDBG_GOTO_IF_FAIL (bo_handle.ptr != NULL, draw_done);
        XDBG_GOTO_IF_FAIL (buf->size > 0, draw_done);

        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);

        if (pPixmap->devPrivate.ptr)
        {
            XDBG_DEBUG (MVA, "vir(%p) size(%d) => pixmap(%p)\n",
                        bo_handle.ptr, buf->size, pPixmap->devPrivate.ptr);

            memcpy (pPixmap->devPrivate.ptr, bo_handle.ptr, buf->size);
        }

        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

        tbm_bo_unmap (buf->bo[0]);
    }
    else /* FOURCC_ST12 */
    {
        PixmapPtr pPixmap = _secVirtualVideoGetPixmap (pPort->pDraw);
        XV_DATA xv_data = {0,};

        _secVirtualVideoSetSecure (pPort, buf->secure);

        XV_INIT_DATA (&xv_data);

        if (buf->phy_addrs[0] > 0)
        {
            xv_data.YBuf = buf->phy_addrs[0];
            xv_data.CbBuf = buf->phy_addrs[1];
            xv_data.CrBuf = buf->phy_addrs[2];

            xv_data.BufType = XV_BUF_TYPE_LEGACY;
        }
        else
        {
            xv_data.YBuf = buf->keys[0];
            xv_data.CbBuf = buf->keys[1];
            xv_data.CrBuf = buf->keys[2];

            xv_data.BufType = XV_BUF_TYPE_DMABUF;
        }

#if 0
        _buffers (pPort);
        ErrorF ("[Xorg] send : %d (%s)\n", xv_data.YBuf, buffers);
#endif

        XDBG_DEBUG (MVA, "still_data(%d,%d,%d) type(%d) \n",
                    xv_data.YBuf, xv_data.CbBuf, xv_data.CrBuf,
                    xv_data.BufType);

        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);

        if (pPixmap->devPrivate.ptr)
            memcpy (pPixmap->devPrivate.ptr, &xv_data, sizeof (XV_DATA));

        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

        _secVirtualVideoAddReturnBuf (pPort, buf);
    }

draw_done:
    DamageDamageRegion (pPort->pDraw, pPort->clipBoxes);
    pPort->need_damage = FALSE;

    SECPtr pSec = SECPTR (pPort->pScrn);
    if ((pSec->dump_mode & XBERC_DUMP_MODE_CA) && pSec->dump_info)
    {
        char file[128];
        static int i;
        snprintf (file, sizeof(file), "capout_stream_%c%c%c%c_%dx%d_%03d.%s",
                  FOURCC_STR(buf->id), buf->width, buf->height, i++,
                  IS_RGB (buf->id)?"bmp":"yuv");
        secUtilDoDumpVBuf (pSec->dump_info, buf, file);
    }

    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_CA)
    {
        CARD32 cur, sub;
        cur = GetTimeInMillis ();
        sub = cur - pPort->prev_time;
        ErrorF ("damage send           : %6ld ms\n", sub);
    }
}

static void
_secVirtualVideoWbDumpFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)user_data;
    SECVideoBuf *vbuf = (SECVideoBuf*)noti_data;

    XDBG_RETURN_IF_FAIL (pPort != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (vbuf));
    XDBG_RETURN_IF_FAIL (vbuf->showing == FALSE);

    XDBG_TRACE (MVA, "dump (%ld,%d,%d,%d)\n", vbuf->stamp,
                vbuf->keys[0], vbuf->keys[1], vbuf->keys[2]);

    if (pPort->need_damage)
    {
        _secVirtualVideoDraw (pPort, vbuf);
    }

    return;
}

static void
_secVirtualVideoWbDumpDoneFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)user_data;

    if (!pPort)
        return;

    XDBG_DEBUG (MVA, "close wb after ipp event done\n");

    XDBG_RETURN_IF_FAIL (pPort->wb != NULL);

    secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbStopFunc);
    secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbDumpFunc);
    secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbDumpDoneFunc);
    secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbCloseFunc);

    secWbClose (pPort->wb);
    pPort->wb = NULL;
}

static void
_secVirtualVideoWbStopFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)user_data;

    if (!pPort)
        return;

    if (pPort->need_damage)
    {
        SECVideoBuf *black = _secVirtualVideoGetBlackBuffer (pPort);
        XDBG_TRACE (MVA, "black buffer(%d) return: wb stop\n", (black)?black->keys[0]:0);
        _secVirtualVideoDraw (pPort, black);
    }
}

static void
_secVirtualVideoWbCloseFunc (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)user_data;

    if (!pPort)
        return;

    pPort->wb = NULL;
}

static void
_secVirtualVideoStreamOff (SECPortPrivPtr pPort)
{
    SECLayer *layer;

    XDBG_TRACE (MVA, "STREAM_OFF!\n");

    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (pPort->wb)
    {
        secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbStopFunc);
        secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbDumpFunc);
        secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbDumpDoneFunc);
        secWbRemoveNotifyFunc (pPort->wb, _secVirtualVideoWbCloseFunc);

        secWbClose (pPort->wb);
        pPort->wb = NULL;
    }

    if (pPort->id != FOURCC_RGB32)
        _secVirtualVideoRemoveReturnBufAll (pPort);

    layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (layer)
        secLayerRemoveNotifyFunc (layer, _secVirtualVideoLayerNotifyFunc);

    if (pPort->need_damage)
    {
        /* all callbacks has been removed from wb/layer. We can't expect
         * any event. So we send black image at the end.
         */
        SECVideoBuf *black = _secVirtualVideoGetBlackBuffer (pPort);
        XDBG_TRACE (MVA, "black buffer(%d) return: stream off\n", (black)?black->keys[0]:0);
        _secVirtualVideoDraw (pPort, black);
    }

    _secVirtualVideoCloseOutBuffer (pPort);

    if (pPort->clipBoxes)
    {
        RegionDestroy (pPort->clipBoxes);
        pPort->clipBoxes = NULL;
    }

    pPort->pDraw = NULL;
    pPort->capture = CAPTURE_MODE_NONE;
    pPort->id = FOURCC_RGB32;
    pPort->secure = FALSE;
    pPort->data_type = DATA_TYPE_UI;
    pPort->need_damage = FALSE;

    if (pPort->putstill_on)
    {
        pPort->putstill_on = FALSE;
        XDBG_SECURE (MVA, "pPort(%d) putstill off. \n", pPort->index);
    }
}

static int
_secVirtualVideoAddDrawableEvent (SECPortPrivPtr pPort)
{
    SECVideoResource *resource;
    void *ptr;
    int ret = 0;

    XDBG_RETURN_VAL_IF_FAIL (pPort->pScrn != NULL, BadImplementation);
    XDBG_RETURN_VAL_IF_FAIL (pPort->pDraw != NULL, BadImplementation);

    ptr = NULL;
    ret = dixLookupResourceByType (&ptr, pPort->pDraw->id,
                             event_drawable_type, NULL, DixWriteAccess);
    if (ret == Success)
        return Success;

    resource = malloc (sizeof (SECVideoResource));
    if (resource == NULL)
        return BadAlloc;

    if (!AddResource (pPort->pDraw->id, event_drawable_type, resource))
    {
        free (resource);
        return BadAlloc;
    }

    XDBG_TRACE (MVA, "id(0x%08lx). \n", pPort->pDraw->id);

    resource->id = pPort->pDraw->id;
    resource->type = event_drawable_type;
    resource->pPort = pPort;
    resource->pScrn = pPort->pScrn;

    return Success;
}

static int
_secVirtualVideoRegisterEventDrawableGone (void *data, XID id)
{
    SECVideoResource *resource = (SECVideoResource*)data;

    XDBG_TRACE (MVA, "id(0x%08lx). \n", id);

    if (!resource)
        return Success;

    if (!resource->pPort || !resource->pScrn)
        return Success;

    resource->pPort->pDraw = NULL;

    SECVirtualVideoStop (resource->pScrn, (pointer)resource->pPort, 1);

    free(resource);

    return Success;
}

static Bool
_secVirtualVideoRegisterEventResourceTypes (void)
{
    event_drawable_type = CreateNewResourceType (_secVirtualVideoRegisterEventDrawableGone,
                                                 "Sec Virtual Video Drawable");

    if (!event_drawable_type)
        return FALSE;

    return TRUE;
}

static tbm_bo
_secVirtualVideoGetFrontBo (SECPortPrivPtr pPort, int connector_type)
{
    xf86CrtcConfigPtr pCrtcConfig;
    int i;

    pCrtcConfig = XF86_CRTC_CONFIG_PTR (pPort->pScrn);
    XDBG_RETURN_VAL_IF_FAIL (pCrtcConfig != NULL, NULL);

    for (i = 0; i < pCrtcConfig->num_output; i++)
    {
        xf86OutputPtr pOutput = pCrtcConfig->output[i];
        SECOutputPrivPtr pOutputPriv = pOutput->driver_private;

        if (pOutputPriv->mode_output->connector_type == connector_type)
        {
            if (pOutput->crtc)
            {
                SECCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;
                return pCrtcPriv->front_bo;
            }
            else
                XDBG_ERROR (MVA, "no crtc.\n");
        }
    }

    return NULL;
}

static SECVideoBuf*
_secVirtualVideoGetBlackBuffer (SECPortPrivPtr pPort)
{
    int i;

    if (!pPort->outbuf[0])
    {
        XDBG_RETURN_VAL_IF_FAIL (pPort->pDraw != NULL, NULL);
        _secVirtualVideoEnsureOutBuffers (pPort->pScrn, pPort, pPort->id,
                                          pPort->pDraw->width, pPort->pDraw->height);
    }

    for (i = 0; i < pPort->outbuf_num; i++)
    {
        if (pPort->outbuf[i] && !pPort->outbuf[i]->showing)
        {
            if (pPort->outbuf[i]->dirty)
                secUtilClearVideoBuffer (pPort->outbuf[i]);

            return pPort->outbuf[i];
        }
    }

    XDBG_ERROR (MVA, "now all buffers are in showing\n");

    return NULL;
}

static Bool
_secVirtualVideoEnsureOutBuffers (ScrnInfoPtr pScrn, SECPortPrivPtr pPort, int id, int width, int height)
{
    SECPtr pSec = SECPTR (pScrn);
    int i;

    if (pPort->display == DISPLAY_EXTERNAL)
        pPort->outbuf_num = BUF_NUM_EXTERNAL;
    else
        pPort->outbuf_num = BUF_NUM_STREAM;

    if (!pPort->outbuf)
    {
        pPort->outbuf = (SECVideoBuf**)calloc(pPort->outbuf_num, sizeof (SECVideoBuf*));
        XDBG_RETURN_VAL_IF_FAIL (pPort->outbuf != NULL, FALSE);
    }

    for (i = 0; i < pPort->outbuf_num; i++)
    {
        int scanout;

        if (pPort->outbuf[i])
            continue;

        XDBG_RETURN_VAL_IF_FAIL (width > 0, FALSE);
        XDBG_RETURN_VAL_IF_FAIL (height > 0, FALSE);

        if (pPort->display == DISPLAY_LCD)
            scanout = FALSE;
        else
            scanout = pSec->scanout;

        /* pPort->pScrn can be NULL if XvPutStill isn't called. */
        pPort->outbuf[i] = secUtilAllocVideoBuffer (pScrn, id,
                                                    width, height,
                                                    scanout, TRUE, pPort->secure);

        XDBG_GOTO_IF_FAIL (pPort->outbuf[i] != NULL, ensure_buffer_fail);
    }

    return TRUE;

ensure_buffer_fail:
    _secVirtualVideoCloseOutBuffer (pPort);

    return FALSE;
}

static Bool
_secVirtualVideoEnsureDstBuffer (SECPortPrivPtr pPort)
{
    if (pPort->dstbuf)
    {
        secUtilClearVideoBuffer (pPort->dstbuf);
        return TRUE;
    }

    pPort->dstbuf = secUtilAllocVideoBuffer (pPort->pScrn, FOURCC_RGB32,
                                                pPort->pDraw->width,
                                                pPort->pDraw->height,
                                                FALSE, FALSE, pPort->secure);
    XDBG_RETURN_VAL_IF_FAIL (pPort->dstbuf != NULL, FALSE);

    return TRUE;
}

static SECVideoBuf*
_secVirtualVideoGetUIBuffer (SECPortPrivPtr pPort, int connector_type)
{
    SECVideoBuf *uibuf = NULL;
    tbm_bo bo[PLANAR_CNT] = {0,};
    SECFbBoDataPtr bo_data = NULL;
    tbm_bo_handle bo_handle;

    bo[0] = _secVirtualVideoGetFrontBo (pPort, connector_type);
    XDBG_RETURN_VAL_IF_FAIL (bo[0] != NULL, NULL);

    tbm_bo_get_user_data (bo[0], TBM_BO_DATA_FB, (void**)&bo_data);
    XDBG_RETURN_VAL_IF_FAIL (bo_data != NULL, NULL);

    uibuf = secUtilCreateVideoBuffer (pPort->pScrn, FOURCC_RGB32,
                                      bo_data->pos.x2 - bo_data->pos.x1,
                                      bo_data->pos.y2 - bo_data->pos.y1,
                                      FALSE);
    XDBG_RETURN_VAL_IF_FAIL (uibuf != NULL, NULL);

    uibuf->bo[0] = tbm_bo_ref (bo[0]);
    XDBG_GOTO_IF_FAIL (uibuf->bo[0] != NULL, fail_get);

    bo_handle = tbm_bo_get_handle (bo[0], TBM_DEVICE_DEFAULT);
    uibuf->handles[0] = bo_handle.u32;

    XDBG_GOTO_IF_FAIL (uibuf->handles[0] > 0, fail_get);

    return uibuf;

fail_get:
    if (uibuf)
        secUtilVideoBufferUnref (uibuf);

    return NULL;
}

static SECVideoBuf*
_secVirtualVideoGetDrawableBuffer (SECPortPrivPtr pPort)
{
    SECVideoBuf *vbuf = NULL;
    PixmapPtr pPixmap = NULL;
    tbm_bo_handle bo_handle;
    SECPixmapPriv *privPixmap;
    Bool need_finish = FALSE;

    XDBG_GOTO_IF_FAIL (pPort->secure == FALSE, fail_get);
    XDBG_GOTO_IF_FAIL (pPort->pDraw != NULL, fail_get);

    pPixmap = _secVirtualVideoGetPixmap (pPort->pDraw);
    XDBG_GOTO_IF_FAIL (pPixmap != NULL, fail_get);

    privPixmap = exaGetPixmapDriverPrivate (pPixmap);
    XDBG_GOTO_IF_FAIL (privPixmap != NULL, fail_get);

    if (!privPixmap->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL (privPixmap->bo != NULL, fail_get);
    }

    vbuf = secUtilCreateVideoBuffer (pPort->pScrn, FOURCC_RGB32,
                                     pPort->pDraw->width,
                                     pPort->pDraw->height,
                                     FALSE);
    XDBG_GOTO_IF_FAIL (vbuf != NULL, fail_get);

    vbuf->bo[0] = tbm_bo_ref (privPixmap->bo);
    bo_handle = tbm_bo_get_handle (privPixmap->bo, TBM_DEVICE_DEFAULT);
    vbuf->handles[0] = bo_handle.u32;

    XDBG_GOTO_IF_FAIL (vbuf->handles[0] > 0, fail_get);

    return vbuf;

fail_get:
    if (pPixmap && need_finish)
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

    if (vbuf)
        secUtilVideoBufferUnref (vbuf);

    return NULL;
}

static void
_secVirtualVideoCloseOutBuffer (SECPortPrivPtr pPort)
{
    int i;

    if (pPort->outbuf)
    {
        for (i = 0; i < pPort->outbuf_num; i++)
        {
            if (pPort->outbuf[i])
                secUtilVideoBufferUnref (pPort->outbuf[i]);
            pPort->outbuf[i] = NULL;
        }

        free (pPort->outbuf);
        pPort->outbuf = NULL;
    }

    if (pPort->dstbuf)
    {
        secUtilVideoBufferUnref (pPort->dstbuf);
        pPort->dstbuf = NULL;
    }

    pPort->outbuf_index = -1;
}

static int
_secVirtualVideoDataType (SECPortPrivPtr pPort)
{
    SECLayer *video_layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_LOWER1);

    return (video_layer) ? DATA_TYPE_VIDEO : DATA_TYPE_UI;
}

static int
_secVirtualVideoPreProcess (ScrnInfoPtr pScrn, SECPortPrivPtr pPort,
                            RegionPtr clipBoxes, DrawablePtr pDraw)
{
    if (pPort->pScrn == pScrn && pPort->pDraw == pDraw &&
        pPort->clipBoxes && clipBoxes && RegionEqual (pPort->clipBoxes, clipBoxes))
        return Success;

    pPort->pScrn = pScrn;
    pPort->pDraw = pDraw;

    if (clipBoxes)
    {
        if (!pPort->clipBoxes)
            pPort->clipBoxes = RegionCreate (NULL, 1);

        XDBG_RETURN_VAL_IF_FAIL (pPort->clipBoxes != NULL, BadAlloc);

        RegionCopy (pPort->clipBoxes, clipBoxes);
    }

    XDBG_TRACE (MVA, "pDraw(0x%x, %dx%d). \n", pDraw->id, pDraw->width, pDraw->height);

    return Success;
}

static int
_secVirtualVideoGetOutBufferIndex (SECPortPrivPtr pPort)
{
    if (!pPort->outbuf)
        return -1;

    pPort->outbuf_index++;
    if (pPort->outbuf_index >= pPort->outbuf_num)
        pPort->outbuf_index = 0;

    return pPort->outbuf_index;
}

static int
_secVirtualVideoSendPortNotify (SECPortPrivPtr pPort, SECPortAttrAtom paa, INT32 value)
{
    SECVideoPortInfo *info;
    Atom atom = None;

    XDBG_RETURN_VAL_IF_FAIL (pPort->pDraw != NULL, BadValue);

    info = _port_info (pPort->pDraw);
    XDBG_RETURN_VAL_IF_FAIL (info != NULL, BadValue);
    XDBG_RETURN_VAL_IF_FAIL (info->pp != NULL, BadValue);

    atom = _secVideoGetPortAtom (paa);
    XDBG_RETURN_VAL_IF_FAIL (atom != None, BadValue);

    XDBG_TRACE (MVA, "paa(%d), value(%d)\n", paa, value);

    return XvdiSendPortNotify (info->pp, atom, value);
}

static Bool
_secVirtualVideoComposite (SECVideoBuf *src, SECVideoBuf *dst,
                           int src_x, int src_y, int src_w, int src_h,
                           int dst_x, int dst_y, int dst_w, int dst_h,
                           Bool composite, int rotate)
{
    xRectangle src_rect = {0,}, dst_rect = {0,};

    XDBG_RETURN_VAL_IF_FAIL (src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src->bo[0] != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst->bo[0] != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src->pitches[0] > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst->pitches[0] > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (IS_RGB (src->id), FALSE);
    XDBG_RETURN_VAL_IF_FAIL (IS_RGB (dst->id), FALSE);

    XDBG_DEBUG (MVA, "comp(%d) src : %ld %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %ld %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                composite, src->stamp, FOURCC_STR (src->id), src->width, src->height,
                src_x, src_y, src_w, src_h,
                dst->stamp, FOURCC_STR (dst->id), dst->width, dst->height,
                dst_x, dst_y, dst_w, dst_h);

    src_rect.x = src_x;
    src_rect.y = src_y;
    src_rect.width = src_w;
    src_rect.height = src_h;
    dst_rect.x = dst_x;
    dst_rect.y = dst_y;
    dst_rect.width = dst_w;
    dst_rect.height = dst_h;

    secUtilConvertBos (src->pScrn,
                       src->bo[0], src->width, src->height, &src_rect, src->pitches[0],
                       dst->bo[0], dst->width, dst->height, &dst_rect, dst->pitches[0],
                       composite, rotate);

    return TRUE;
}

static int
_secVirtualVideoCompositeExtLayers (SECPortPrivPtr pPort)
{
    SECVideoBuf *dst_buf = NULL;
    SECLayer    *lower_layer = NULL;
    SECLayer    *upper_layer = NULL;
    SECVideoBuf *ui_buf = NULL;
    xRectangle   rect = {0,};
    int index;
    Bool comp = FALSE;

    index = _secVirtualVideoGetOutBufferIndex (pPort);
    if (index < 0)
    {
        XDBG_WARNING (MVA, "all out buffers are in use.\n");
        return FALSE;
    }

    lower_layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (lower_layer)
    {
        SECVideoBuf *lower_buf = secLayerGetBuffer (lower_layer);

        if (!lower_buf->secure && lower_buf && VBUF_IS_VALID (lower_buf))
        {
            /* In case of virtual, lower layer already has full-size. */
            dst_buf = lower_buf;
            comp = TRUE;
        }
    }

    if (!dst_buf)
    {
        if (!_secVirtualVideoEnsureDstBuffer (pPort))
            return FALSE;

        dst_buf = pPort->dstbuf;
    }

    /* before compositing, flush all */
    secUtilCacheFlush (pPort->pScrn);

    ui_buf = _secVirtualVideoGetUIBuffer (pPort, DRM_MODE_CONNECTOR_VIRTUAL);
    if (ui_buf)
    {
        XDBG_DEBUG (MVA, "ui : %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                    FOURCC_STR (ui_buf->id),
                    ui_buf->width, ui_buf->height,
                    ui_buf->crop.x, ui_buf->crop.y,
                    ui_buf->crop.width, ui_buf->crop.height,
                    FOURCC_STR (dst_buf->id),
                    dst_buf->width, dst_buf->height,
                    0, 0,
                    dst_buf->width, dst_buf->height);

        if (!_secVirtualVideoComposite (ui_buf, dst_buf,
                                        ui_buf->crop.x, ui_buf->crop.y,
                                        ui_buf->crop.width, ui_buf->crop.height,
                                        0, 0,
                                        dst_buf->width, dst_buf->height,
                                        comp, 0))
        {
            secUtilVideoBufferUnref (ui_buf);
            return FALSE;
        }

        comp = TRUE;
    }

    upper_layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_UPPER);
    if (upper_layer)
    {
        SECVideoBuf *upper_buf = secLayerGetBuffer (upper_layer);

        if (upper_buf && VBUF_IS_VALID (upper_buf))
        {
            secLayerGetRect (upper_layer, &upper_buf->crop, &rect);

            XDBG_DEBUG (MVA, "upper : %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                        FOURCC_STR (upper_buf->id),
                        upper_buf->width, upper_buf->height,
                        upper_buf->crop.x, upper_buf->crop.y,
                        upper_buf->crop.width, upper_buf->crop.height,
                        FOURCC_STR (dst_buf->id),
                        dst_buf->width, dst_buf->height,
                        rect.x, rect.y, rect.width, rect.height);

            _secVirtualVideoComposite (upper_buf, dst_buf,
                                       upper_buf->crop.x, upper_buf->crop.y,
                                       upper_buf->crop.width, upper_buf->crop.height,
                                       rect.x, rect.y, rect.width, rect.height,
                                       comp, 0);
        }
    }

    dst_buf->crop.x = 0;
    dst_buf->crop.y = 0;
    dst_buf->crop.width = dst_buf->width;
    dst_buf->crop.height = dst_buf->height;

    XDBG_RETURN_VAL_IF_FAIL (pPort->outbuf[index] != NULL, FALSE);

    pPort->outbuf[index]->crop.x = 0;
    pPort->outbuf[index]->crop.y = 0;
    pPort->outbuf[index]->crop.width = pPort->outbuf[index]->width;
    pPort->outbuf[index]->crop.height = pPort->outbuf[index]->height;
    _secVirtualVideoComposite (dst_buf, pPort->outbuf[index],
                               0, 0, dst_buf->width, dst_buf->height,
                               0, 0, pPort->outbuf[index]->width, pPort->outbuf[index]->height,
                               FALSE, 0);

    _secVirtualVideoDraw (pPort, pPort->outbuf[index]);

    if (ui_buf)
        secUtilVideoBufferUnref (ui_buf);

    return TRUE;
}

static void
_secVirtualVideoCompositeSubtitle (SECPortPrivPtr pPort, SECVideoBuf *vbuf)
{
    SECLayer    *subtitle_layer;
    SECVideoBuf *subtitle_vbuf;
    xRectangle   src_rect;
    xRectangle   dst_rect;

    subtitle_layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_UPPER);
    if (!subtitle_layer)
        return;

    subtitle_vbuf = secLayerGetBuffer (subtitle_layer);
    if (!subtitle_vbuf || !VBUF_IS_VALID (subtitle_vbuf))
        return;

    CLEAR (src_rect);
    CLEAR (dst_rect);
    secLayerGetRect (subtitle_layer, &src_rect, &dst_rect);

    XDBG_DEBUG (MVA, "subtitle : %dx%d (%d,%d %dx%d) => %dx%d (%d,%d %dx%d)\n",
                subtitle_vbuf->width, subtitle_vbuf->height,
                src_rect.x, src_rect.y, src_rect.width, src_rect.height,
                vbuf->width, vbuf->height,
                dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);

    _secVirtualVideoComposite (subtitle_vbuf, vbuf,
                               src_rect.x, src_rect.y, src_rect.width, src_rect.height,
                               dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height,
                               TRUE, 0);
}

static CARD32
_secVirtualVideoRetireTimeout (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) arg;
    SECModePtr pSecMode;
    int diff;

    if (!pPort)
        return 0;

    pSecMode = (SECModePtr)SECPTR (pPort->pScrn)->pSecMode;

    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    XDBG_ERROR (MVA, "capture(%d) mode(%d) conn(%d) type(%d) status(%x). \n",
                pPort->capture, pSecMode->set_mode, pSecMode->conn_mode,
                pPort->data_type, pPort->status);

    diff = GetTimeInMillis () - pPort->retire_time;
    XDBG_ERROR (MVA, "failed : +++ Retire Timeout!! diff(%d)\n", diff);

    return 0;
}

static void
_secVirtualVideoLayerNotifyFunc (SECLayer *layer, int type, void *type_data, void *data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)data;
    SECVideoBuf *vbuf = (SECVideoBuf*)type_data;
    SECVideoBuf *black;

    secLayerRemoveNotifyFunc (layer, _secVirtualVideoLayerNotifyFunc);

    if (type == LAYER_DESTROYED || type != LAYER_BUF_CHANGED || !vbuf)
        goto fail_layer_noti;

    XDBG_GOTO_IF_FAIL (VBUF_IS_VALID (vbuf), fail_layer_noti);
    XDBG_GOTO_IF_FAIL (vbuf->showing == FALSE, fail_layer_noti);

    XDBG_DEBUG (MVA, "------------------------------\n");

    _secVirtualVideoCompositeSubtitle (pPort, vbuf);
    _secVirtualVideoDraw (pPort, vbuf);
    XDBG_DEBUG (MVA, "------------------------------...\n");

    return;

fail_layer_noti:
    black = _secVirtualVideoGetBlackBuffer (pPort);
    XDBG_TRACE (MVA, "black buffer(%d) return: layer noti. type(%d), vbuf(%p)\n",
                (black)?black->keys[0]:0, type, vbuf);
    _secVirtualVideoDraw (pPort, black);
}

static int
_secVirtualVideoPutStill (SECPortPrivPtr pPort)
{
    SECModePtr pSecMode = (SECModePtr)SECPTR (pPort->pScrn)->pSecMode;
    SECVideoBuf *pix_buf = NULL;
    SECVideoBuf *ui_buf = NULL;
    Bool comp;
    int i;
    CARD32 start = GetTimeInMillis ();

    XDBG_GOTO_IF_FAIL (pPort->secure == FALSE, done_still);

    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    comp = FALSE;

    pix_buf = _secVirtualVideoGetDrawableBuffer (pPort);
    XDBG_GOTO_IF_FAIL (pix_buf != NULL, done_still);

    ui_buf = _secVirtualVideoGetUIBuffer (pPort, DRM_MODE_CONNECTOR_LVDS);
    XDBG_GOTO_IF_FAIL (ui_buf != NULL, done_still);

    tbm_bo_map (pix_buf->bo[0], TBM_DEVICE_2D, TBM_OPTION_WRITE);

    for (i = LAYER_LOWER2; i < LAYER_MAX; i++)
    {
        SECVideoBuf *upper = NULL;
        xRectangle src_rect, dst_rect;
        int vwidth = pSecMode->main_lcd_mode.hdisplay;
        int vheight = pSecMode->main_lcd_mode.vdisplay;
        int rotate;

        if (i == LAYER_DEFAULT)
        {
            upper = secUtilVideoBufferRef (ui_buf);
            tbm_bo_map (upper->bo[0], TBM_DEVICE_2D, TBM_OPTION_READ);

            src_rect.x = src_rect.y = 0;
            src_rect.width = ui_buf->width;
            src_rect.height = ui_buf->height;

            dst_rect.x = dst_rect.y = 0;
            dst_rect.width = ui_buf->width;
            dst_rect.height = ui_buf->height;

            rotate = 0;
        }
        else
        {
            SECLayer *layer = secLayerFind (LAYER_OUTPUT_LCD, (SECLayerPos)i);
            int off_x = 0, off_y = 0;
            SECVideoPrivPtr pVideo = SECPTR(pPort->pScrn)->pVideoPriv;

            if (!layer)
                continue;

            upper = secUtilVideoBufferRef (secLayerGetBuffer (layer));
            if (!upper || !VBUF_IS_VALID (upper))
                continue;

            secLayerGetRect (layer, &src_rect, &dst_rect);
            secLayerGetOffset (layer, &off_x, &off_y);
            dst_rect.x += off_x;
            dst_rect.y += off_y;

            rotate = (360 - pVideo->screen_rotate_degree) % 360;

            /* rotate upper_rect */
            secUtilRotateArea (&vwidth, &vheight, &dst_rect, rotate);
        }

        /* scale upper_rect */
        secUtilScaleRect (vwidth, vheight, pix_buf->width, pix_buf->height, &dst_rect);

        XDBG_DEBUG (MVA, "%dx%d(%d,%d, %dx%d) => %dx%d(%d,%d, %dx%d) :comp(%d) r(%d)\n",
                    upper->width, upper->height,
                    src_rect.x, src_rect.y, src_rect.width, src_rect.height,
                    pix_buf->width, pix_buf->height,
                    dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height,
                    comp, rotate);

        if (!_secVirtualVideoComposite (upper, pix_buf,
                                        src_rect.x, src_rect.y,
                                        src_rect.width, src_rect.height,
                                        dst_rect.x, dst_rect.y,
                                        dst_rect.width, dst_rect.height,
                                        comp, rotate))
        {
            if (i == LAYER_DEFAULT)
                tbm_bo_unmap (upper->bo[0]);
            tbm_bo_unmap (pix_buf->bo[0]);
            goto done_still;
        }

        if (i == LAYER_DEFAULT)
            tbm_bo_unmap (upper->bo[0]);

        secUtilVideoBufferUnref (upper);

        comp = TRUE;
    }

    XDBG_TRACE (MVA, "make still: %ldms\n", GetTimeInMillis() - start);

    tbm_bo_unmap (pix_buf->bo[0]);

done_still:

    secUtilCacheFlush (pPort->pScrn);

    if (pix_buf)
        secUtilVideoBufferUnref (pix_buf);
    if (ui_buf)
        secUtilVideoBufferUnref (ui_buf);

    DamageDamageRegion (pPort->pDraw, pPort->clipBoxes);
    pPort->need_damage = FALSE;

    SECPtr pSec = SECPTR (pPort->pScrn);
    if ((pSec->dump_mode & XBERC_DUMP_MODE_CA) && pSec->dump_info)
    {
        PixmapPtr pPixmap = _secVirtualVideoGetPixmap (pPort->pDraw);
        char file[128];
        static int i;
        snprintf (file, sizeof(file), "capout_still_%03d.bmp", i++);
        secUtilDoDumpPixmaps (pSec->dump_info, pPixmap, file);
    }

    return Success;
}

static int
_secVirtualVideoPutWB (SECPortPrivPtr pPort)
{
    SECPtr pSec = SECPTR (pPort->pScrn);

    XDBG_RETURN_VAL_IF_FAIL (pPort->pScrn != NULL, BadImplementation);
    XDBG_RETURN_VAL_IF_FAIL (pPort->pDraw != NULL, BadImplementation);

    if (!_secVirtualVideoEnsureOutBuffers (pPort->pScrn, pPort, pPort->id, pPort->pDraw->width, pPort->pDraw->height))
        return BadAlloc;

    if (!pPort->wb)
    {
        int scanout;

        if (secWbIsOpened ())
        {
            XDBG_ERROR (MVA, "Fail : wb open. \n");
            return BadRequest;
        }

        if (pPort->display == DISPLAY_LCD)
            scanout = FALSE;
        else
            scanout = pSec->scanout;

        /* For capture mode, we don't need to create contiguous buffer.
         * Rotation should be considered when wb begins.
         */
        pPort->wb = secWbOpen (pPort->pScrn, pPort->id,
                               pPort->pDraw->width, pPort->pDraw->height,
                               scanout, 60,
                               (pPort->rotate_off)?FALSE:TRUE);
        XDBG_RETURN_VAL_IF_FAIL (pPort->wb != NULL, BadAlloc);

        secWbSetBuffer (pPort->wb, pPort->outbuf, pPort->outbuf_num);

        XDBG_TRACE (MVA, "wb(%p) start. \n", pPort->wb);

        if (!secWbStart (pPort->wb))
        {
            secWbClose (pPort->wb);
            pPort->wb = NULL;
            return BadAlloc;
        }
        secWbAddNotifyFunc (pPort->wb, WB_NOTI_STOP,
                            _secVirtualVideoWbStopFunc, pPort);
        secWbAddNotifyFunc (pPort->wb, WB_NOTI_IPP_EVENT,
                            _secVirtualVideoWbDumpFunc, pPort);
        if (pPort->capture == CAPTURE_MODE_STILL)
            secWbAddNotifyFunc (pPort->wb, WB_NOTI_IPP_EVENT_DONE,
                                _secVirtualVideoWbDumpDoneFunc, pPort);
        secWbAddNotifyFunc (pPort->wb, WB_NOTI_CLOSED,
                            _secVirtualVideoWbCloseFunc, pPort);
    }

    /* no available buffer, need to return buffer by client. */
    if (!secWbIsRunning ())
    {
        XDBG_WARNING (MVA, "wb is stopped.\n");
        return BadRequest;
    }

    /* no available buffer, need to return buffer by client. */
    if (!secWbCanDequeueBuffer (pPort->wb))
    {
        XDBG_TRACE (MVA, "no available buffer\n");
        return BadRequest;
    }

    XDBG_TRACE (MVA, "wb(%p), running(%d). \n", pPort->wb, secWbIsRunning ());

    return Success;
}

static int
_secVirtualVideoPutVideoOnly (SECPortPrivPtr pPort)
{
    SECLayer *layer;
    SECVideoBuf *vbuf;
    int i;

    XDBG_RETURN_VAL_IF_FAIL (pPort->display == DISPLAY_EXTERNAL, BadRequest);
    XDBG_RETURN_VAL_IF_FAIL (pPort->capture == CAPTURE_MODE_STREAM, BadRequest);

    layer = secLayerFind (LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (!layer)
        return BadRequest;

    for (i = 0; i < pPort->outbuf_num; i++)
    {
        if (!pPort->outbuf[i]->showing)
            break;
    }

    if (i == pPort->outbuf_num)
    {
        XDBG_ERROR (MVA, "now all buffers are in showing\n");
        return BadRequest;
    }

    vbuf = secLayerGetBuffer (layer);
    /* if layer is just created, vbuf can't be null. */
    if (!vbuf || !VBUF_IS_VALID (vbuf))
    {
        SECVideoBuf *black = _secVirtualVideoGetBlackBuffer (pPort);
        XDBG_RETURN_VAL_IF_FAIL (black != NULL, BadRequest);

        XDBG_TRACE (MVA, "black buffer(%d) return: vbuf invalid\n", black->keys[0]);
        _secVirtualVideoDraw (pPort, black);
        return Success;
    }

    /* Wait the next frame if it's same as previous one */
    if (_secVirtualVideoFindReturnBuf (pPort, vbuf->keys[0]))
    {
        secLayerAddNotifyFunc (layer, _secVirtualVideoLayerNotifyFunc, pPort);
        XDBG_DEBUG (MVA, "wait notify.\n");
        return Success;
    }

    _secVirtualVideoCompositeSubtitle (pPort, vbuf);
    _secVirtualVideoDraw (pPort, vbuf);

    return Success;
}

static int
_secVirtualVideoPutExt (SECPortPrivPtr pPort)
{
    if (_secVirtualVideoCompositeExtLayers (pPort))
        return Success;

    return BadRequest;
}

static int
SECVirtualVideoGetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32      *value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _secVideoGetPortAtom (PAA_FORMAT))
    {
        *value = pPort->id;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_CAPTURE))
    {
        *value = pPort->capture;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_DISPLAY))
    {
        *value = pPort->display;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_ROTATE_OFF))
    {
        *value = pPort->rotate_off;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_DATA_TYPE))
    {
        *value = pPort->data_type;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_SECURE))
    {
        *value = pPort->secure;
        return Success;
    }
    return BadMatch;
}

static int
SECVirtualVideoSetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32       value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _secVideoGetPortAtom (PAA_FORMAT))
    {
        if (!_secVirtualVideoIsSupport ((unsigned int)value))
        {
            XDBG_ERROR (MVA, "id(%c%c%c%c) not supported.\n", FOURCC_STR (value));
            return BadRequest;
        }

        pPort->id = (unsigned int)value;
        XDBG_DEBUG (MVA, "id(%d) \n", value);
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_CAPTURE))
    {
        if (value < CAPTURE_MODE_NONE || value >= CAPTURE_MODE_MAX)
        {
            XDBG_ERROR (MVA, "capture value(%d) is out of range\n", value);
            return BadRequest;
        }

        pPort->capture = value;
        XDBG_DEBUG (MVA, "capture(%d) \n", pPort->capture);
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_DISPLAY))
    {
        XDBG_DEBUG (MVA, "display: %d \n", pPort->display);
        pPort->display = value;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_ROTATE_OFF))
    {
        XDBG_DEBUG (MVA, "ROTATE_OFF: %d! \n", pPort->rotate_off);
        pPort->rotate_off = value;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_SECURE))
    {
        XDBG_TRACE (MVA, "not implemented 'secure' attr. (%d) \n", pPort->secure);
//        pPort->secure = value;
        return Success;
    }
    else if (attribute == _secVideoGetPortAtom (PAA_RETBUF))
    {
        RetBufInfo *info;

        if (!pPort->pDraw)
            return Success;

        info = _secVirtualVideoFindReturnBuf (pPort, value);
        if (!info)
        {
            XDBG_WARNING (MVA, "wrong gem name(%d) returned\n", value);
            return Success;
        }

        if (info->vbuf && info->vbuf->need_reset)
            secUtilClearVideoBuffer (info->vbuf);

        _secVirtualVideoRemoveReturnBuf (pPort, info);
#if 0
        _buffers (pPort);
        ErrorF ("[Xorg] retbuf : %ld (%s)\n", value, buffers);
#endif

        return Success;
    }

    return Success;
}

/* vid_w, vid_h : no meaning for us. not using.
 * dst_w, dst_h : size to hope for PutStill.
 * p_w, p_h     : real size for PutStill.
 */
static void
SECVirtualVideoQueryBestSize (ScrnInfoPtr pScrn,
                              Bool motion,
                              short vid_w, short vid_h,
                              short dst_w, short dst_h,
                              unsigned int *p_w, unsigned int *p_h,
                              pointer data)
{
    SECModePtr pSecMode = (SECModePtr)SECPTR (pScrn)->pSecMode;
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (pPort->display == DISPLAY_EXTERNAL)
    {
        if (p_w)
            *p_w = pSecMode->ext_connector_mode.hdisplay;
        if (p_h)
            *p_h = pSecMode->ext_connector_mode.vdisplay;
    }
    else
    {
        if (p_w)
            *p_w = (unsigned int)dst_w;
        if (p_h)
            *p_h = (unsigned int)dst_h;
    }
}

/* vid_x, vid_y, vid_w, vid_h : no meaning for us. not using.
 * drw_x, drw_y, dst_w, dst_h : no meaning for us. not using.
 * Only pDraw's size is used.
 */
static int
SECVirtualVideoPutStill (ScrnInfoPtr pScrn,
                         short vid_x, short vid_y, short drw_x, short drw_y,
                         short vid_w, short vid_h, short drw_w, short drw_h,
                         RegionPtr clipBoxes, pointer data, DrawablePtr pDraw )
{
    SECPtr pSec = SECPTR (pScrn);
    SECModePtr pSecMode = (SECModePtr)SECPTR (pScrn)->pSecMode;
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;
    int ret = BadRequest;

    XDBG_GOTO_IF_FAIL (pPort->need_damage == FALSE, put_still_fail);

    if (pPort->capture == CAPTURE_MODE_STILL && pPort->display == DISPLAY_EXTERNAL)
    {
        XDBG_ERROR (MVA, "not implemented to capture still of external display. \n");
        return BadImplementation;
    }

    if (pPort->display == DISPLAY_EXTERNAL && pSecMode->conn_mode != DISPLAY_CONN_MODE_VIRTUAL)
    {
        XDBG_ERROR (MVA, "virtual display not connected!. \n");
        return BadRequest;
    }

#if 0
    ErrorF ("[Xorg] PutStill\n");
#endif

    XDBG_DEBUG (MVA, "*************************************** \n");

    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_CA)
    {
        CARD32 cur, sub;
        cur = GetTimeInMillis ();
        sub = cur - pPort->prev_time;
        pPort->prev_time = cur;
        ErrorF ("getstill interval     : %6ld ms\n", sub);
    }

    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (pSec->pVideoPriv->no_retbuf)
        _secVirtualVideoRemoveReturnBufAll (pPort);

    pPort->retire_timer = TimerSet (pPort->retire_timer, 0, 4000,
                                    _secVirtualVideoRetireTimeout,
                                    pPort);
    XDBG_GOTO_IF_FAIL (pPort->id > 0, put_still_fail);

    pPort->status = 0;
    pPort->retire_time = GetTimeInMillis ();

    ret = _secVirtualVideoPreProcess (pScrn, pPort, clipBoxes, pDraw);
    XDBG_GOTO_IF_FAIL (ret == Success, put_still_fail);

    ret = _secVirtualVideoAddDrawableEvent (pPort);
    XDBG_GOTO_IF_FAIL (ret == Success, put_still_fail);

    /* check drawable */
    XDBG_RETURN_VAL_IF_FAIL (pDraw->type == DRAWABLE_PIXMAP, BadPixmap);

    if (!pPort->putstill_on)
    {
        pPort->putstill_on = TRUE;
        XDBG_SECURE (MVA, "pPort(%d) putstill on. secure(%d), capture(%d), format(%c%c%c%c)\n",
                     pPort->index, pPort->secure, pPort->capture, FOURCC_STR (pPort->id), 60);
    }

    pPort->need_damage = TRUE;

    if (pPort->capture == CAPTURE_MODE_STILL && pPort->display == DISPLAY_LCD)
    {
        XDBG_DEBUG (MVA, "still mode.\n");

        if (1)
            ret = _secVirtualVideoPutStill (pPort);
        else
            /* camera buffer can't be mapped. we should use WB to capture screen */
            ret = _secVirtualVideoPutWB (pPort);

        XDBG_GOTO_IF_FAIL (ret == Success, put_still_fail);
    }
    else if (pPort->capture == CAPTURE_MODE_STREAM && pPort->display == DISPLAY_LCD)
    {
        XDBG_DEBUG (MVA, "stream mode.\n");
        if (SECPTR (pScrn)->isLcdOff)
        {
            XDBG_TRACE (MVA, "DPMS status: off. \n");
            ret = BadRequest;
            goto put_still_fail;
        }

        ret = _secVirtualVideoPutWB (pPort);
        if (ret != Success)
            goto put_still_fail;
    }
    else if (pPort->capture == CAPTURE_MODE_STREAM && pPort->display == DISPLAY_EXTERNAL)
    {
        int old_data_type = pPort->data_type;
        SECVideoBuf *black;

        switch (pSecMode->set_mode)
        {
        case DISPLAY_SET_MODE_OFF:
            XDBG_DEBUG (MVA, "display mode is off. \n");
            black = _secVirtualVideoGetBlackBuffer (pPort);
            XDBG_RETURN_VAL_IF_FAIL (black != NULL, BadRequest);
            XDBG_DEBUG (MVA, "black buffer(%d) return: lcd off\n", black->keys[0]);
            _secVirtualVideoDraw (pPort, black);
            ret = Success;
            goto put_still_fail;

        case DISPLAY_SET_MODE_CLONE:
            pPort->data_type = _secVirtualVideoDataType (pPort);

            if (pPort->data_type != old_data_type)
                _secVirtualVideoSendPortNotify (pPort, PAA_DATA_TYPE, pPort->data_type);

            if (pPort->data_type == DATA_TYPE_UI)
            {
                XDBG_DEBUG (MVA, "clone mode.\n");

                ret = _secVirtualVideoPutWB (pPort);
                if (ret != Success)
                    goto put_still_fail;
            }
            else
            {
                XDBG_DEBUG (MVA, "video only mode.\n");
                ret = _secVirtualVideoPutVideoOnly (pPort);
                if (ret != Success)
                    goto put_still_fail;
            }
            break;

        case DISPLAY_SET_MODE_EXT:
            XDBG_DEBUG (MVA, "desktop mode.\n");

            if (pSecMode->ext_connector_mode.hdisplay != pDraw->width ||
                pSecMode->ext_connector_mode.vdisplay != pDraw->height)
            {
                XDBG_ERROR (MVA, "drawble should have %dx%d size. mode(%d), conn(%d)\n",
                            pSecMode->ext_connector_mode.hdisplay,
                            pSecMode->ext_connector_mode.vdisplay,
                            pSecMode->set_mode, pSecMode->conn_mode);
                ret = BadRequest;
                goto put_still_fail;
            }

            ret = _secVirtualVideoPutExt (pPort);
            if (ret != Success)
                goto put_still_fail;
            break;

        default:
            break;
        }
    }
    else
    {
        XDBG_NEVER_GET_HERE (MVA);
        ret = BadRequest;
        goto put_still_fail;
    }

    XDBG_DEBUG (MVA, "***************************************.. \n");
    return Success;

put_still_fail:
    pPort->need_damage = FALSE;

    if (pPort->retire_timer)
    {
        TimerFree (pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    XDBG_DEBUG (MVA, "***************************************.. \n");

    return ret;
}

static void
SECVirtualVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    _secVirtualVideoStreamOff (pPort);
}

static int
SECVirtualVideoDDPutStill (ClientPtr client,
                           DrawablePtr pDraw,
                           XvPortPtr pPort,
                           GCPtr pGC,
                           INT16 vid_x, INT16 vid_y,
                           CARD16 vid_w, CARD16 vid_h,
                           INT16 drw_x, INT16 drw_y, CARD16 drw_w, CARD16 drw_h)
{
    SECVideoPortInfo *info = _port_info (pDraw);
    int ret;

    if (info)
    {
        info->client = client;
        info->pp = pPort;
    }

    ret = ddPutStill (client, pDraw, pPort, pGC,
                      vid_x, vid_y, vid_w, vid_h,
                      drw_x, drw_y, drw_w, drw_h);

    return ret;
}

XF86VideoAdaptorPtr
secVideoSetupVirtualVideo (ScreenPtr pScreen)
{
    XF86VideoAdaptorPtr pAdaptor;
    SECPortPrivPtr pPort;
    int i;

    pAdaptor = calloc (1, sizeof (XF86VideoAdaptorRec) +
                       (sizeof (DevUnion) + sizeof (SECPortPriv)) * SEC_MAX_PORT);
    if (!pAdaptor)
        return NULL;

    dummy_encoding[0].width = pScreen->width;
    dummy_encoding[0].height = pScreen->height;

    pAdaptor->type = XvWindowMask | XvPixmapMask | XvInputMask | XvStillMask;
    pAdaptor->flags = 0;
    pAdaptor->name = "SEC Virtual Video";
    pAdaptor->nEncodings = sizeof (dummy_encoding) / sizeof (XF86VideoEncodingRec);
    pAdaptor->pEncodings = dummy_encoding;
    pAdaptor->nFormats = NUM_FORMATS;
    pAdaptor->pFormats = formats;
    pAdaptor->nPorts = SEC_MAX_PORT;
    pAdaptor->pPortPrivates = (DevUnion*)(&pAdaptor[1]);

    pPort = (SECPortPrivPtr) (&pAdaptor->pPortPrivates[SEC_MAX_PORT]);

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        pAdaptor->pPortPrivates[i].ptr = &pPort[i];
        pPort[i].index = i;
        pPort[i].id = FOURCC_RGB32;
        pPort[i].outbuf_index = -1;

        xorg_list_init (&pPort[i].retbuf_info);
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;
    pAdaptor->nImages = NUM_IMAGES;
    pAdaptor->pImages = images;

    pAdaptor->GetPortAttribute     = SECVirtualVideoGetPortAttribute;
    pAdaptor->SetPortAttribute     = SECVirtualVideoSetPortAttribute;
    pAdaptor->QueryBestSize        = SECVirtualVideoQueryBestSize;
    pAdaptor->PutStill             = SECVirtualVideoPutStill;
    pAdaptor->StopVideo            = SECVirtualVideoStop;

    if (!_secVirtualVideoRegisterEventResourceTypes ())
    {
        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
        xf86DrvMsg (pScrn->scrnIndex, X_ERROR, "Failed to register EventResourceTypes. \n");
        return NULL;
    }

    return pAdaptor;
}

void
secVirtualVideoDpms (ScrnInfoPtr pScrn, Bool on)
{
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pSec->pVideoPriv->pAdaptor[1];
    int i;

    if (on)
        return;

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        SECPortPrivPtr pPort = (SECPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->wb)
        {
            secWbClose (pPort->wb);
            pPort->wb = NULL;
        }
    }
}

void
secVirtualVideoReplacePutStillFunc (ScreenPtr pScreen)
{
    int i;

    XvScreenPtr xvsp = dixLookupPrivate (&pScreen->devPrivates,
                                         XvGetScreenKey());
    if (!xvsp)
        return;

    for (i = 1; i < xvsp->nAdaptors; i++)
    {
        XvAdaptorPtr pAdapt = xvsp->pAdaptors + i;
        if (pAdapt->ddPutStill)
        {
            ddPutStill = pAdapt->ddPutStill;
            pAdapt->ddPutStill = SECVirtualVideoDDPutStill;
            break;
        }
    }

    if (!dixRegisterPrivateKey (VideoVirtualPortKey, PRIVATE_WINDOW, sizeof (SECVideoPortInfo)))
        return;
    if (!dixRegisterPrivateKey (VideoVirtualPortKey, PRIVATE_PIXMAP, sizeof (SECVideoPortInfo)))
        return;
}

void
secVirtualVideoGetBuffers (ScrnInfoPtr pScrn, int id, int width, int height, SECVideoBuf ***vbufs, int *bufnum)
{
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pSec->pVideoPriv->pAdaptor[1];
    int i;

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        SECPortPrivPtr pPort = (SECPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->pDraw)
        {
            XDBG_RETURN_IF_FAIL (pPort->id == id);
            XDBG_RETURN_IF_FAIL (pPort->pDraw->width == width);
            XDBG_RETURN_IF_FAIL (pPort->pDraw->height == height);
        }

        if (!_secVirtualVideoEnsureOutBuffers (pScrn, pPort, id, width, height))
            return;

        *vbufs = pPort->outbuf;
        *bufnum = pPort->outbuf_num;
    }
}
