/*
 * xserver-xorg-video-exynos
 *
 * Copyright 2004 Keith Packard
 * Copyright 2005 Eric Anholt
 * Copyright 2006 Nokia Corporation
 * Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.
 *
 * Contact: Boram Park <boram1288.park@samsung.com>
 * Contact: Oleksandr Rozov<o.rozov@samsung.com>
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

#include "exynos.h"
#include "exynos_util.h"
#include "exynos_wb.h"
#include "exynos_crtc.h"
#include "exynos_converter.h"
#include "exynos_output.h"
#include "exynos_video.h"
#include "exynos_video_fourcc.h"
#include "exynos_video_virtual.h"
#include "exynos_video_tvout.h"
#include "exynos_display.h"
#include "exynos_xberc.h"

#include "xv_types.h"

#define DEV_INDEX   2
#define BUF_NUM_EXTERNAL 5
#define BUF_NUM_STREAM  3

enum {
    CAPTURE_MODE_NONE,
    CAPTURE_MODE_STILL,
    CAPTURE_MODE_STREAM,
    CAPTURE_MODE_MAX,
};

enum {
    DISPLAY_MAIN,
    DISPLAY_EXTERNAL,
};

enum {
    DATA_TYPE_NONE,
    DATA_TYPE_UI,
    DATA_TYPE_VIDEO,
    DATA_TYPE_MAX,
};

static unsigned int support_formats[] = {
    FOURCC_RGB32,
    FOURCC_ST12,
    FOURCC_SN12,
};

static XF86VideoEncodingRec dummy_encoding[] = {
    {0, "XV_IMAGE", -1, -1, {1, 1}},
    {1, "XV_IMAGE", 2560, 2560, {1, 1}},
};

static XF86ImageRec images[] = {
    XVIMAGE_RGB32,
    XVIMAGE_SN12,
    XVIMAGE_ST12,
};

static XF86VideoFormatRec formats[] = {
    {16, TrueColor},
    {24, TrueColor},
    {32, TrueColor},
};

static XF86AttributeRec attributes[] = {
    {0, 0, 0x7fffffff, "_USER_WM_PORT_ATTRIBUTE_FORMAT"},
    {0, 0, CAPTURE_MODE_MAX, "_USER_WM_PORT_ATTRIBUTE_CAPTURE"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_DISPLAY"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_ROTATE_OFF"},
    {0, 0, DATA_TYPE_MAX, "_USER_WM_PORT_ATTRIBUTE_DATA_TYPE"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_SECURE"},
    {0, 0, 0x7fffffff, "_USER_WM_PORT_ATTRIBUTE_RETURN_BUFFER"},
};

typedef enum {
    PAA_MIN,
    PAA_FORMAT,
    PAA_CAPTURE,
    PAA_DISPLAY,
    PAA_ROTATE_OFF,
    PAA_DATA_TYPE,
    PAA_SECURE,
    PAA_RETBUF,
    PAA_MAX
} EXYNOSPortAttrAtom;

static struct {
    EXYNOSPortAttrAtom paa;
    const char *name;
    Atom atom;
} atoms[] = {
    {
    PAA_FORMAT, "_USER_WM_PORT_ATTRIBUTE_FORMAT", None}, {
    PAA_CAPTURE, "_USER_WM_PORT_ATTRIBUTE_CAPTURE", None}, {
    PAA_DISPLAY, "_USER_WM_PORT_ATTRIBUTE_DISPLAY", None}, {
    PAA_ROTATE_OFF, "_USER_WM_PORT_ATTRIBUTE_ROTATE_OFF", None}, {
    PAA_DATA_TYPE, "_USER_WM_PORT_ATTRIBUTE_DATA_TYPE", None}, {
    PAA_SECURE, "_USER_WM_PORT_ATTRIBUTE_SECURE", None}, {
PAA_RETBUF, "_USER_WM_PORT_ATTRIBUTE_RETURN_BUFFER", None},};

typedef struct _RetBufInfo {
    EXYNOSVideoBuf *vbuf;
    int type;
    struct xorg_list link;
} RetBufInfo;

/* EXYNOS port information structure */
typedef struct {
    /* index */
    int index;

    /* port attribute */
    int id;
    int capture;
    int display;
    Bool exynosure;
    Bool data_type;
    Bool rotate_off;

    /* information from outside */
    ScrnInfoPtr pScrn;
    DrawablePtr pDraw;
    RegionPtr clipBoxes;

    /* writeback */
    EXYNOSWb *wb;
    Bool need_close_wb;

    /* video */
    EXYNOSCvt *cvt;
    EXYNOSCvt *cvt2;

    EXYNOSVideoBuf *dstbuf;
    EXYNOSVideoBuf **outbuf;
    int outbuf_num;
    int outbuf_index;

    struct xorg_list retbuf_info;
    Bool need_damage;

    OsTimerPtr retire_timer;
    Bool putstill_on;

    unsigned int status;
    CARD32 retire_time;
    CARD32 prev_time;

    /*convert dst buffer */
    EXYNOSVideoBuf *capture_dstbuf;
    Bool wait_rgb_convert;
    int active_connector;

    struct xorg_list link;
} EXYNOSPortPriv, *EXYNOSPortPrivPtr;

static RESTYPE event_drawable_type;

typedef struct _EXYNOSVideoResource {
    XID id;
    RESTYPE type;
    EXYNOSPortPrivPtr pPort;
    ScrnInfoPtr pScrn;
} EXYNOSVideoResource;

#define EXYNOS_MAX_PORT        1

#define NUM_IMAGES        (sizeof(images) / sizeof(images[0]))
#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

static DevPrivateKeyRec video_virtual_port_key;

#define VideoVirtualPortKey (&video_virtual_port_key)
#define GetPortInfo(pDraw) ((EXYNOSVideoPortInfo*)dixLookupPrivate(&(pDraw)->devPrivates, VideoVirtualPortKey))

typedef struct _EXYNOSVideoPortInfo {
    ClientPtr client;
    XvPortPtr pp;
} EXYNOSVideoPortInfo;

static int (*ddPutStill) (ClientPtr, DrawablePtr, struct _XvPortRec *, GCPtr,
                          INT16, INT16, CARD16, CARD16,
                          INT16, INT16, CARD16, CARD16);

static void EXYNOSVirtualVideoStop(ScrnInfoPtr pScrn, pointer data, Bool exit);
static void _exynosVirtualVideoCloseOutBuffer(EXYNOSPortPrivPtr pPort);
static void _exynosVirtualVideoLayerNotifyFunc(EXYNOSLayer * layer, int type,
                                               void *type_data, void *data);
static void _exynosVirtualVideoWbDumpFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                                          void *noti_data, void *user_data);
static void _exynosVirtualVideoWbStopFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                                          void *noti_data, void *user_data);
static EXYNOSVideoBuf *_exynosVirtualVideoGetBlackBuffer(EXYNOSPortPrivPtr
                                                         pPort);
static Bool _exynosVirtualVideoEnsureOutBuffers(ScrnInfoPtr pScrn,
                                                EXYNOSPortPrivPtr pPort, int id,
                                                int width, int height);
static void _exynosVirtualVideoWbCloseFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                                           void *noti_data, void *user_data);

static Bool


exynosCaptureConvertImage(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * vbuf,
                          int csc_range);
static void _exynosCaptureCloseConverter(EXYNOSPortPrivPtr pPort);
static void _exynosCaptureEnsureConverter(EXYNOSPortPrivPtr pPort);

static EXYNOSVideoPortInfo *
_port_info(DrawablePtr pDraw)
{
    if (!pDraw)
        return NULL;

    if (pDraw->type == DRAWABLE_WINDOW)
        return GetPortInfo((WindowPtr) pDraw);
    else
        return GetPortInfo((PixmapPtr) pDraw);
}

static Atom
_exynosVideoGetPortAtom(EXYNOSPortAttrAtom paa)
{
    int i;

    XDBG_RETURN_VAL_IF_FAIL(paa > PAA_MIN && paa < PAA_MAX, None);

    for (i = 0; i < NUM_ATOMS; i++) {
        if (paa == atoms[i].paa) {
            if (atoms[i].atom == None)
                atoms[i].atom = MakeAtom(atoms[i].name,
                                         strlen(atoms[i].name), TRUE);

            return atoms[i].atom;
        }
    }

    XDBG_ERROR(MVA, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static void
_exynosVirtualVideoSetSecure(EXYNOSPortPrivPtr pPort, Bool exynosure)
{
    EXYNOSVideoPortInfo *info;

    exynosure = (exynosure > 0) ? TRUE : FALSE;

    if (pPort->exynosure == exynosure)
        return;

    pPort->exynosure = exynosure;

    XDBG_TRACE(MVA, "exynosure(%d) \n", exynosure);

    info = _port_info(pPort->pDraw);
    if (!info || !info->pp)
        return;

    XvdiSendPortNotify(info->pp, _exynosVideoGetPortAtom(PAA_SECURE),
                       exynosure);
}

static PixmapPtr
_exynosVirtualVideoGetPixmap(DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pDraw->pScreen->GetWindowPixmap((WindowPtr) pDraw);
    else
        return (PixmapPtr) pDraw;
}

static Bool
_exynosVirtualVideoIsSupport(unsigned int id)
{
    int i;

    for (i = 0; i < sizeof(support_formats) / sizeof(unsigned int); i++)
        if (support_formats[i] == id)
            return TRUE;

    return FALSE;
}

#if 0
static char buffers[1024];

static void
_buffers(EXYNOSPortPrivPtr pPort)
{
    RetBufInfo *cur = NULL, *next = NULL;

    CLEAR(buffers);
    xorg_list_for_each_entry_safe(cur, next, &pPort->retbuf_info, link) {
        if (cur->vbuf)
            snprintf(buffers, 1024, "%s %d", buffers, cur->vbuf->keys[0]);
    }
}
#endif

static RetBufInfo *
_exynosVirtualVideoFindReturnBuf(EXYNOSPortPrivPtr pPort, unsigned int key)
{
    RetBufInfo *cur = NULL, *next = NULL;

    XDBG_RETURN_VAL_IF_FAIL(pPort->capture != CAPTURE_MODE_STILL, NULL);

    xorg_list_for_each_entry_safe(cur, next, &pPort->retbuf_info, link) {
        if (cur->vbuf && cur->vbuf->keys[0] == key)
            return cur;
        else if (cur->vbuf && cur->vbuf->phy_addrs[0] == key)
            return cur;
    }

    return NULL;
}

static Bool
_exynosVirtualVideoAddReturnBuf(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * vbuf)
{
    RetBufInfo *info;

    XDBG_RETURN_VAL_IF_FAIL(pPort->capture != CAPTURE_MODE_STILL, FALSE);

    info = _exynosVirtualVideoFindReturnBuf(pPort, vbuf->keys[0]);
    XDBG_RETURN_VAL_IF_FAIL(info == NULL, FALSE);

    info = calloc(1, sizeof(RetBufInfo));
    XDBG_RETURN_VAL_IF_FAIL(info != NULL, FALSE);

    info->vbuf = exynosUtilVideoBufferRef(vbuf);
    XDBG_GOTO_IF_FAIL(info->vbuf != NULL, fail);
    info->vbuf->showing = TRUE;

    XDBG_DEBUG(MVA, "retbuf (%" PRIuPTR ",%d,%d,%d) added.\n", vbuf->stamp,
               vbuf->keys[0], vbuf->keys[1], vbuf->keys[2]);

    xorg_list_add(&info->link, &pPort->retbuf_info);

    return TRUE;

 fail:
    if (info)
        free(info);

    return FALSE;
}

static void
_exynosVirtualVideoRemoveReturnBuf(EXYNOSPortPrivPtr pPort, RetBufInfo * info)
{
    XDBG_RETURN_IF_FAIL(pPort->capture != CAPTURE_MODE_STILL);
    XDBG_RETURN_IF_FAIL(info != NULL);
    XDBG_RETURN_IF_FAIL(info->vbuf != NULL);

    info->vbuf->showing = FALSE;

    XDBG_DEBUG(MVA, "retbuf (%" PRIuPTR ",%d,%d,%d) removed.\n",
               info->vbuf->stamp, info->vbuf->keys[0], info->vbuf->keys[1],
               info->vbuf->keys[2]);

    if (pPort->wb)
        exynosWbQueueBuffer(pPort->wb, info->vbuf);

    exynosUtilVideoBufferUnref(info->vbuf);
    xorg_list_del(&info->link);
    free(info);
}

static void
_exynosVirtualVideoRemoveReturnBufAll(EXYNOSPortPrivPtr pPort)
{
    RetBufInfo *cur = NULL, *next = NULL;

    XDBG_RETURN_IF_FAIL(pPort->capture != CAPTURE_MODE_STILL);

    xorg_list_for_each_entry_safe(cur, next, &pPort->retbuf_info, link) {
        _exynosVirtualVideoRemoveReturnBuf(pPort, cur);
    }
}

static void
_exynosVirtualVideoDraw(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * buf)
{
    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (!pPort->pDraw) {
        XDBG_TRACE(MVA, "drawable gone!\n");
        return;
    }

    XDBG_RETURN_IF_FAIL(pPort->need_damage == TRUE);
    XDBG_GOTO_IF_FAIL(VBUF_IS_VALID(buf), draw_done);

    XDBG_TRACE(MVA, "%c%c%c%c, %dx%d. \n",
               FOURCC_STR(buf->id), buf->width, buf->height);

    if (pPort->id == FOURCC_RGB32) {
        PixmapPtr pPixmap = _exynosVirtualVideoGetPixmap(pPort->pDraw);
        tbm_bo_handle bo_handle;

        XDBG_GOTO_IF_FAIL(buf->exynosure == FALSE, draw_done);

        if (pPort->pDraw->width != buf->width ||
            pPort->pDraw->height != buf->height) {
            XDBG_ERROR(MVA, "not matched. (%dx%d != %dx%d) \n",
                       pPort->pDraw->width, pPort->pDraw->height, buf->width,
                       buf->height);
            goto draw_done;
        }

        bo_handle = tbm_bo_map(buf->bo[0], TBM_DEVICE_CPU, TBM_OPTION_READ);
        XDBG_GOTO_IF_FAIL(bo_handle.ptr != NULL, draw_done);
        XDBG_GOTO_IF_FAIL(buf->size > 0, draw_done);

        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);

        if (pPixmap->devPrivate.ptr) {
            XDBG_DEBUG(MVA, "vir(%p) size(%d) => pixmap(%p)\n",
                       bo_handle.ptr, buf->size, pPixmap->devPrivate.ptr);

            memcpy(pPixmap->devPrivate.ptr, bo_handle.ptr, buf->size);
        }

        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

        tbm_bo_unmap(buf->bo[0]);
    }
    else {                      /* FOURCC_ST12 */

        PixmapPtr pPixmap = _exynosVirtualVideoGetPixmap(pPort->pDraw);
        XV_DATA xv_data = { 0, };

        _exynosVirtualVideoSetSecure(pPort, buf->exynosure);

        XV_INIT_DATA(&xv_data);

        if (buf->phy_addrs[0] > 0) {
            xv_data.YBuf = buf->phy_addrs[0];
            xv_data.CbBuf = buf->phy_addrs[1];
            xv_data.CrBuf = buf->phy_addrs[2];

            xv_data.BufType = XV_BUF_TYPE_LEGACY;
        }
        else {
            xv_data.YBuf = buf->keys[0];
            xv_data.CbBuf = buf->keys[1];
            xv_data.CrBuf = buf->keys[2];

            xv_data.BufType = XV_BUF_TYPE_DMABUF;
        }

#if 0
        _buffers(pPort);
        ErrorF("[Xorg] send : %d (%s)\n", xv_data.YBuf, buffers);
#endif

        XDBG_DEBUG(MVA, "still_data(%d,%d,%d) type(%d) \n",
                   xv_data.YBuf, xv_data.CbBuf, xv_data.CrBuf, xv_data.BufType);

        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);

        if (pPixmap->devPrivate.ptr)
            memcpy(pPixmap->devPrivate.ptr, &xv_data, sizeof(XV_DATA));

        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

        _exynosVirtualVideoAddReturnBuf(pPort, buf);
    }

 draw_done:
    DamageDamageRegion(pPort->pDraw, pPort->clipBoxes);
    pPort->need_damage = FALSE;

    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if ((pExynos->dump_mode & XBERC_DUMP_MODE_CA) && pExynos->dump_info) {
        char file[128];
        static int i;

        snprintf(file, sizeof(file), "capout_stream_%c%c%c%c_%dx%d_%03d.%s",
                 FOURCC_STR(buf->id), buf->width, buf->height, i++,
                 IS_RGB(buf->id) ? "bmp" : "yuv");
        exynosUtilDoDumpVBuf(pExynos->dump_info, buf, file);
    }

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_CA) {
        CARD32 cur, sub;

        cur = GetTimeInMillis();
        sub = cur - pPort->prev_time;
        ErrorF("damage send           : %6" PRIXID " ms\n", sub);
    }
}

static void
_exynosVirtualVideoWbDumpFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                              void *noti_data, void *user_data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) user_data;
    EXYNOSVideoBuf *vbuf = (EXYNOSVideoBuf *) noti_data;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(vbuf));
    XDBG_RETURN_IF_FAIL(vbuf->showing == FALSE);

    XDBG_TRACE(MVA, "dump (%" PRIuPTR ",%d,%d,%d)\n", vbuf->stamp,
               vbuf->keys[0], vbuf->keys[1], vbuf->keys[2]);

    if (pPort->need_damage) {
        _exynosVirtualVideoDraw(pPort, vbuf);
    }

    return;
}

static void
_exynosVirtualVideoWbDumpDoneFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                                  void *noti_data, void *user_data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) user_data;

    if (!pPort)
        return;

    XDBG_DEBUG(MVA, "close wb after ipp event done\n");

    XDBG_RETURN_IF_FAIL(pPort->wb != NULL);

    exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbStopFunc);
    exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbDumpFunc);
    exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbDumpDoneFunc);
    exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbCloseFunc);
    if (pPort->need_close_wb)
        exynosWbClose(pPort->wb);
    pPort->wb = NULL;
}

static void
_exynosVirtualVideoWbStopFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                              void *noti_data, void *user_data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) user_data;

    if (!pPort)
        return;

    if (pPort->need_damage) {
        EXYNOSVideoBuf *black = _exynosVirtualVideoGetBlackBuffer(pPort);

        XDBG_TRACE(MVA, "black buffer(%d) return: wb stop\n",
                   (black) ? black->keys[0] : 0);
        _exynosVirtualVideoDraw(pPort, black);
    }
}

static void
_exynosVirtualVideoWbCloseFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                               void *noti_data, void *user_data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) user_data;

    if (!pPort)
        return;

    pPort->wb = NULL;
}

static void
_exynosVirtualVideoStreamOff(EXYNOSPortPrivPtr pPort)
{
    EXYNOSLayer *layer;

    XDBG_TRACE(MVA, "STREAM_OFF!\n");

    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (pPort->wb) {
        exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbStopFunc);
        exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbDumpFunc);
        exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbDumpDoneFunc);
        exynosWbRemoveNotifyFunc(pPort->wb, _exynosVirtualVideoWbCloseFunc);
        if (pPort->need_close_wb)
            exynosWbClose(pPort->wb);
        pPort->wb = NULL;
    }

    if (pPort->id != FOURCC_RGB32)
        _exynosVirtualVideoRemoveReturnBufAll(pPort);

    layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (layer)
        exynosLayerRemoveNotifyFunc(layer, _exynosVirtualVideoLayerNotifyFunc);

    if (pPort->need_damage) {
        /* all callbacks has been removed from wb/layer. We can't expect
         * any event. So we send black image at the end.
         */
        EXYNOSVideoBuf *black = _exynosVirtualVideoGetBlackBuffer(pPort);

        XDBG_TRACE(MVA, "black buffer(%d) return: stream off\n",
                   (black) ? black->keys[0] : 0);
        _exynosVirtualVideoDraw(pPort, black);
    }

    _exynosVirtualVideoCloseOutBuffer(pPort);

    if (pPort->clipBoxes) {
        RegionDestroy(pPort->clipBoxes);
        pPort->clipBoxes = NULL;
    }

    pPort->pDraw = NULL;
    pPort->capture = CAPTURE_MODE_NONE;
    pPort->id = FOURCC_RGB32;
    pPort->exynosure = FALSE;
    pPort->data_type = DATA_TYPE_UI;
    pPort->need_damage = FALSE;

    if (pPort->putstill_on) {
        pPort->putstill_on = FALSE;
        XDBG_SECURE(MVA, "pPort(%d) putstill off. \n", pPort->index);
    }
}

static int
_exynosVirtualVideoAddDrawableEvent(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoResource *resource;
    void *ptr;
    int ret = 0;

    XDBG_RETURN_VAL_IF_FAIL(pPort->pScrn != NULL, BadImplementation);
    XDBG_RETURN_VAL_IF_FAIL(pPort->pDraw != NULL, BadImplementation);

    ptr = NULL;
    ret = dixLookupResourceByType(&ptr, pPort->pDraw->id,
                                  event_drawable_type, NULL, DixWriteAccess);
    if (ret == Success)
        return Success;

    resource = malloc(sizeof(EXYNOSVideoResource));
    if (resource == NULL)
        return BadAlloc;

    if (!AddResource(pPort->pDraw->id, event_drawable_type, resource)) {
        free(resource);
        return BadAlloc;
    }

    XDBG_TRACE(MVA, "id(0x%08lx). \n", pPort->pDraw->id);

    resource->id = pPort->pDraw->id;
    resource->type = event_drawable_type;
    resource->pPort = pPort;
    resource->pScrn = pPort->pScrn;

    return Success;
}

static int
_exynosVirtualVideoRegisterEventDrawableGone(void *data, XID id)
{
    EXYNOSVideoResource *resource = (EXYNOSVideoResource *) data;

    XDBG_TRACE(MVA, "id(0x%08lx). \n", id);

    if (!resource)
        return Success;

    if (!resource->pPort || !resource->pScrn)
        return Success;

    resource->pPort->pDraw = NULL;

    EXYNOSVirtualVideoStop(resource->pScrn, (pointer) resource->pPort, 1);

    free(resource);

    return Success;
}

static Bool
_exynosVirtualVideoRegisterEventResourceTypes(void)
{
    event_drawable_type =
        CreateNewResourceType(_exynosVirtualVideoRegisterEventDrawableGone,
                              "Sec Virtual Video Drawable");

    if (!event_drawable_type)
        return FALSE;

    return TRUE;
}

static tbm_bo
_exynosVirtualVideoGetFrontBo(EXYNOSPortPrivPtr pPort, int connector_type)
{
    xf86CrtcConfigPtr pCrtcConfig;
    int i;

    pCrtcConfig = XF86_CRTC_CONFIG_PTR(pPort->pScrn);
    XDBG_RETURN_VAL_IF_FAIL(pCrtcConfig != NULL, NULL);

    for (i = 0; i < pCrtcConfig->num_output; i++) {
        xf86OutputPtr pOutput = pCrtcConfig->output[i];
        EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;

        if (pOutputPriv->mode_output->connector_type == connector_type) {
            if (pOutput->crtc) {
                EXYNOSCrtcPrivPtr pCrtcPriv = pOutput->crtc->driver_private;

                return pCrtcPriv->front_bo;
            }
            else
                XDBG_ERROR(MVA, "no crtc.\n");
        }
    }

    return NULL;
}

static EXYNOSVideoBuf *
_exynosVirtualVideoGetBlackBuffer(EXYNOSPortPrivPtr pPort)
{
    int i;

    if (!pPort->outbuf) {
        XDBG_RETURN_VAL_IF_FAIL(pPort->pDraw != NULL, NULL);
        _exynosVirtualVideoEnsureOutBuffers(pPort->pScrn, pPort, pPort->id,
                                            pPort->pDraw->width,
                                            pPort->pDraw->height);
    }

    for (i = 0; i < pPort->outbuf_num; i++) {
        if (pPort->outbuf[i] && !pPort->outbuf[i]->showing) {
            if (pPort->outbuf[i]->dirty)
                exynosUtilClearVideoBuffer(pPort->outbuf[i]);

            return pPort->outbuf[i];
        }
    }

    XDBG_ERROR(MVA, "now all buffers are in showing\n");

    return NULL;
}

static Bool
_exynosVirtualVideoEnsureOutBuffers(ScrnInfoPtr pScrn, EXYNOSPortPrivPtr pPort,
                                    int id, int width, int height)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    int i;

    if (pPort->display == DISPLAY_EXTERNAL)
        pPort->outbuf_num = BUF_NUM_EXTERNAL;
    else
        pPort->outbuf_num = BUF_NUM_STREAM;

    if (!pPort->outbuf) {
        pPort->outbuf =
            (EXYNOSVideoBuf **) calloc(pPort->outbuf_num,
                                       sizeof(EXYNOSVideoBuf *));
        XDBG_RETURN_VAL_IF_FAIL(pPort->outbuf != NULL, FALSE);
    }

    for (i = 0; i < pPort->outbuf_num; i++) {
        int scanout;

        if (pPort->outbuf[i])
            continue;

        XDBG_RETURN_VAL_IF_FAIL(width > 0, FALSE);
        XDBG_RETURN_VAL_IF_FAIL(height > 0, FALSE);

        if (pPort->display == DISPLAY_MAIN)
            scanout = FALSE;
        else
            scanout = pExynos->scanout;

        /* pPort->pScrn can be NULL if XvPutStill isn't called. */
        pPort->outbuf[i] = exynosUtilAllocVideoBuffer(pScrn, id,
                                                      width, height,
                                                      scanout, TRUE,
                                                      pPort->exynosure);

        XDBG_GOTO_IF_FAIL(pPort->outbuf[i] != NULL, ensure_buffer_fail);
    }

    return TRUE;

 ensure_buffer_fail:
    _exynosVirtualVideoCloseOutBuffer(pPort);

    return FALSE;
}

static Bool
_exynosVirtualVideoEnsureDstBuffer(EXYNOSPortPrivPtr pPort)
{
    if (pPort->dstbuf) {
        exynosUtilClearVideoBuffer(pPort->dstbuf);
        return TRUE;
    }

    pPort->dstbuf = exynosUtilAllocVideoBuffer(pPort->pScrn, FOURCC_RGB32,
                                               pPort->pDraw->width,
                                               pPort->pDraw->height,
                                               FALSE, FALSE, pPort->exynosure);
    XDBG_RETURN_VAL_IF_FAIL(pPort->dstbuf != NULL, FALSE);

    return TRUE;
}

static EXYNOSVideoBuf *
_exynosVirtualVideoGetUIBuffer(EXYNOSPortPrivPtr pPort, int connector_type)
{
    EXYNOSVideoBuf *uibuf = NULL;
    tbm_bo bo[PLANAR_CNT] = { 0, };
    EXYNOSFbBoDataPtr bo_data = NULL;
    tbm_bo_handle bo_handle;

    bo[0] = _exynosVirtualVideoGetFrontBo(pPort, connector_type);
    XDBG_RETURN_VAL_IF_FAIL(bo[0] != NULL, NULL);

    tbm_bo_get_user_data(bo[0], TBM_BO_DATA_FB, (void **) &bo_data);
    XDBG_RETURN_VAL_IF_FAIL(bo_data != NULL, NULL);

    uibuf = exynosUtilCreateVideoBuffer(pPort->pScrn, FOURCC_RGB32,
                                        bo_data->pos.x2 - bo_data->pos.x1,
                                        bo_data->pos.y2 - bo_data->pos.y1,
                                        FALSE);
    XDBG_RETURN_VAL_IF_FAIL(uibuf != NULL, NULL);

    uibuf->bo[0] = tbm_bo_ref(bo[0]);
    XDBG_GOTO_IF_FAIL(uibuf->bo[0] != NULL, fail_get);

    bo_handle = tbm_bo_get_handle(bo[0], TBM_DEVICE_DEFAULT);
    uibuf->handles[0] = bo_handle.u32;

    XDBG_GOTO_IF_FAIL(uibuf->handles[0] > 0, fail_get);

    return uibuf;

 fail_get:
    if (uibuf)
        exynosUtilVideoBufferUnref(uibuf);

    return NULL;
}

static EXYNOSVideoBuf *
_exynosVirtualVideoGetDrawableBuffer(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoBuf *vbuf = NULL;
    PixmapPtr pPixmap = NULL;
    tbm_bo_handle bo_handle;
    EXYNOSPixmapPriv *privPixmap;
    Bool need_finish = FALSE;

    XDBG_GOTO_IF_FAIL(pPort->exynosure == FALSE, fail_get);
    XDBG_GOTO_IF_FAIL(pPort->pDraw != NULL, fail_get);

    pPixmap = _exynosVirtualVideoGetPixmap(pPort->pDraw);
    XDBG_GOTO_IF_FAIL(pPixmap != NULL, fail_get);

    privPixmap = exaGetPixmapDriverPrivate(pPixmap);
    XDBG_GOTO_IF_FAIL(privPixmap != NULL, fail_get);

    if (!privPixmap->bo) {
        need_finish = TRUE;
        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL(privPixmap->bo != NULL, fail_get);
    }

    vbuf = exynosUtilCreateVideoBuffer(pPort->pScrn, FOURCC_RGB32,
                                       pPort->pDraw->width,
                                       pPort->pDraw->height, FALSE);
    XDBG_GOTO_IF_FAIL(vbuf != NULL, fail_get);

    vbuf->bo[0] = tbm_bo_ref(privPixmap->bo);
    bo_handle = tbm_bo_get_handle(privPixmap->bo, TBM_DEVICE_DEFAULT);
    vbuf->handles[0] = bo_handle.u32;

    XDBG_GOTO_IF_FAIL(vbuf->handles[0] > 0, fail_get);

    return vbuf;

 fail_get:
    if (pPixmap && need_finish)
        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

    if (vbuf)
        exynosUtilVideoBufferUnref(vbuf);

    return NULL;
}

static void
_exynosVirtualVideoCloseOutBuffer(EXYNOSPortPrivPtr pPort)
{
    int i;

    if (pPort->outbuf) {
        for (i = 0; i < pPort->outbuf_num; i++) {
            if (pPort->outbuf[i])
                exynosUtilVideoBufferUnref(pPort->outbuf[i]);
            pPort->outbuf[i] = NULL;
        }

        free(pPort->outbuf);
        pPort->outbuf = NULL;
    }

    if (pPort->dstbuf) {
        exynosUtilVideoBufferUnref(pPort->dstbuf);
        pPort->dstbuf = NULL;
    }

    pPort->outbuf_index = -1;
}

static int
_exynosVirtualVideoDataType(EXYNOSPortPrivPtr pPort)
{
    int ret = DATA_TYPE_NONE;

    if (exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1)) {
        ret += DATA_TYPE_VIDEO;
    }
    if (exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_DEFAULT)) {
        ret += DATA_TYPE_UI;
    }
    return ret;
}

static int
_exynosVirtualVideoPreProcess(ScrnInfoPtr pScrn, EXYNOSPortPrivPtr pPort,
                              RegionPtr clipBoxes, DrawablePtr pDraw)
{
    if (pPort->pScrn == pScrn && pPort->pDraw == pDraw &&
        pPort->clipBoxes && clipBoxes &&
        RegionEqual(pPort->clipBoxes, clipBoxes))
        return Success;

    pPort->pScrn = pScrn;
    pPort->pDraw = pDraw;

    if (clipBoxes) {
        if (!pPort->clipBoxes)
            pPort->clipBoxes = RegionCreate(NULL, 1);

        XDBG_RETURN_VAL_IF_FAIL(pPort->clipBoxes != NULL, BadAlloc);

        RegionCopy(pPort->clipBoxes, clipBoxes);
    }

    XDBG_TRACE(MVA, "pDraw(0x%lx, %dx%d). \n", pDraw->id, pDraw->width,
               pDraw->height);

    return Success;
}

static int
_exynosVirtualVideoGetOutBufferIndex(EXYNOSPortPrivPtr pPort)
{
    if (!pPort->outbuf)
        return -1;

    pPort->outbuf_index++;
    if (pPort->outbuf_index >= pPort->outbuf_num)
        pPort->outbuf_index = 0;

    return pPort->outbuf_index;
}

static int
_exynosVirtualVideoSendPortNotify(EXYNOSPortPrivPtr pPort,
                                  EXYNOSPortAttrAtom paa, INT32 value)
{
    EXYNOSVideoPortInfo *info;
    Atom atom = None;

    XDBG_RETURN_VAL_IF_FAIL(pPort->pDraw != NULL, BadValue);

    info = _port_info(pPort->pDraw);
    XDBG_RETURN_VAL_IF_FAIL(info != NULL, BadValue);
    XDBG_RETURN_VAL_IF_FAIL(info->pp != NULL, BadValue);

    atom = _exynosVideoGetPortAtom(paa);
    XDBG_RETURN_VAL_IF_FAIL(atom != None, BadValue);

    XDBG_TRACE(MVA, "paa(%d), value(%d)\n", paa, (int) value);

    return XvdiSendPortNotify(info->pp, atom, value);
}

static Bool
_exynosVirtualVideoComposite(EXYNOSVideoBuf * src, EXYNOSVideoBuf * dst,
                             int src_x, int src_y, int src_w, int src_h,
                             int dst_x, int dst_y, int dst_w, int dst_h,
                             Bool composite, int rotate)
{
    xRectangle src_rect = { 0, }, dst_rect = {
    0,};

    XDBG_RETURN_VAL_IF_FAIL(src != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src->bo[0] != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst->bo[0] != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src->pitches[0] > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst->pitches[0] > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(IS_RGB(src->id) || IS_YUV(src->id), FALSE);
    XDBG_RETURN_VAL_IF_FAIL(IS_RGB(dst->id) || IS_YUV(dst->id), FALSE);

    XDBG_DEBUG(MVA,
               "comp(%d) src : %" PRIuPTR
               " %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %" PRIuPTR
               " %c%c%c%c  %dx%d (%d,%d %dx%d)\n", composite, src->stamp,
               FOURCC_STR(src->id), src->width, src->height, src_x, src_y,
               src_w, src_h, dst->stamp, FOURCC_STR(dst->id), dst->width,
               dst->height, dst_x, dst_y, dst_w, dst_h);

    src_rect.x = src_x;
    src_rect.y = src_y;
    src_rect.width = src_w;
    src_rect.height = src_h;
    dst_rect.x = dst_x;
    dst_rect.y = dst_y;
    dst_rect.width = dst_w;
    dst_rect.height = dst_h;

    exynosUtilConvertBos(src->pScrn, src->id,
                         src->bo[0], src->width, src->height, &src_rect,
                         src->pitches[0], dst->bo[0], dst->width, dst->height,
                         &dst_rect, dst->pitches[0], composite, rotate);

    return TRUE;
}

static int
_exynosVirtualStillCompositeExtLayers(EXYNOSPortPrivPtr pPort,
                                      int connector_type, Bool complete)
{
    EXYNOSLayer *lower_layer = NULL;
    EXYNOSLayer *upper_layer = NULL;
    EXYNOSVideoBuf *pix_buf = NULL;
    EXYNOSVideoBuf *ui_buf = NULL;
    EXYNOSVideoBuf *dst_buf = NULL;
    xRectangle rect = { 0, };
    xRectangle src_rect, dst_rect;
    int off_x = 0, off_y = 0;
    Bool comp = FALSE;
    int rotate = 0;

    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pPort->pScrn)->pExynosMode;
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

    lower_layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (lower_layer) {
        exynosLayerGetRect(lower_layer, &src_rect, &dst_rect);
        exynosLayerGetOffset(lower_layer, &off_x, &off_y);
        dst_rect.x += off_x;
        dst_rect.y += off_y;
    }
    //convertion is finished ?
    if (!complete) {
        /* check if operation in process */
        if (pPort->wait_rgb_convert)
            goto convert_ipp_still;

        if (lower_layer) {
            EXYNOSVideoBuf *lower_buf = exynosLayerGetBuffer(lower_layer);

            XDBG_GOTO_IF_FAIL(lower_buf != NULL, done_ipp_still);
            if (!IS_RGB(lower_buf->id) || !IS_YUV(lower_buf->id)) {
                /*convert vbuf to RGB format */
                pPort->capture_dstbuf = NULL;
                if (exynosCaptureConvertImage(pPort, lower_buf, 0)) {
                    /* convertion in process */
                    pPort->wait_rgb_convert = TRUE;
                    goto convert_ipp_still;
                }

            }

            if (!lower_buf->exynosure && lower_buf && VBUF_IS_VALID(lower_buf)) {
                /* In case of virtual, lower layer already has full-size. */
                dst_buf = lower_buf;
                comp = TRUE;
            }
        }
    }
    else {
        if (pPort->capture_dstbuf)
            dst_buf = pPort->capture_dstbuf;
        pPort->wait_rgb_convert = FALSE;
    }

    pix_buf = _exynosVirtualVideoGetDrawableBuffer(pPort);
    XDBG_GOTO_IF_FAIL(pix_buf != NULL, done_ipp_still);

    tbm_bo_map(pix_buf->bo[0], TBM_DEVICE_2D, TBM_OPTION_WRITE);

    int vwidth = pExynosMode->ext_connector_mode.hdisplay;
    int vheight = pExynosMode->ext_connector_mode.vdisplay;

    rotate = (360 - pVideo->screen_rotate_degree) % 360;

    /* rotate upper_rect */
    exynosUtilRotateArea(&vwidth, &vheight, &dst_rect, rotate);

    /* scale upper_rect */
    exynosUtilScaleRect(vwidth, vheight, pix_buf->width, pix_buf->height,
                        &dst_rect);

    /* before compositing, flush all */
    exynosUtilCacheFlush(pPort->pScrn);

    comp = TRUE;

    if (dst_buf) {

        if (!_exynosVirtualVideoComposite(dst_buf, pix_buf,
                                          0, 0, dst_buf->width, dst_buf->height,
                                          dst_rect.x, dst_rect.y,
                                          dst_rect.width, dst_rect.height,
                                          comp, rotate)) {
            exynosUtilVideoBufferUnref(dst_buf);
            goto done_ipp_still;
        }
    }

    ui_buf = _exynosVirtualVideoGetUIBuffer(pPort, connector_type);     //Need to choose active connector DRM_MODE_CONNECTOR_VIRTUAL
    XDBG_GOTO_IF_FAIL(ui_buf != NULL, done_ipp_still);

    if (ui_buf) {
        ui_buf = exynosUtilVideoBufferRef(ui_buf);
        tbm_bo_map(ui_buf->bo[0], TBM_DEVICE_2D, TBM_OPTION_READ);

        src_rect.x = src_rect.y = 0;
        src_rect.width = ui_buf->width;
        src_rect.height = ui_buf->height;

        dst_rect.x = dst_rect.y = 0;
        dst_rect.width = ui_buf->width;
        dst_rect.height = ui_buf->height;

        /* scale upper_rect */
        exynosUtilScaleRect(vwidth, vheight, pix_buf->width, pix_buf->height,
                            &dst_rect);

        XDBG_DEBUG(MVA,
                   "%dx%d(%d,%d, %dx%d) => %dx%d(%d,%d, %dx%d) :comp(%d) r(%d)\n",
                   ui_buf->width, ui_buf->height, src_rect.x, src_rect.y,
                   src_rect.width, src_rect.height, pix_buf->width,
                   pix_buf->height, dst_rect.x, dst_rect.y, dst_rect.width,
                   dst_rect.height, comp, rotate);

        if (!_exynosVirtualVideoComposite(ui_buf, pix_buf,
                                          ui_buf->crop.x, ui_buf->crop.y,
                                          ui_buf->crop.width,
                                          ui_buf->crop.height, dst_rect.x,
                                          dst_rect.y, dst_rect.width,
                                          dst_rect.height, comp, rotate)) {
            exynosUtilVideoBufferUnref(ui_buf);
            goto done_ipp_still;
        }

//        comp = TRUE;
    }

    upper_layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_UPPER);
    if (upper_layer) {
        EXYNOSVideoBuf *upper_buf = exynosLayerGetBuffer(upper_layer);

        if (upper_buf && VBUF_IS_VALID(upper_buf)) {
            exynosLayerGetRect(upper_layer, &upper_buf->crop, &rect);

            XDBG_DEBUG(MVA,
                       "upper : %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                       FOURCC_STR(upper_buf->id), upper_buf->width,
                       upper_buf->height, upper_buf->crop.x, upper_buf->crop.y,
                       upper_buf->crop.width, upper_buf->crop.height,
                       FOURCC_STR(dst_buf->id), dst_buf->width, dst_buf->height,
                       rect.x, rect.y, rect.width, rect.height);

            _exynosVirtualVideoComposite(upper_buf, pix_buf,
                                         upper_buf->crop.x, upper_buf->crop.y,
                                         upper_buf->crop.width,
                                         upper_buf->crop.height, rect.x, rect.y,
                                         rect.width, rect.height, comp, 0);
        }
    }

    DamageDamageRegion(pPort->pDraw, pPort->clipBoxes);
    pPort->need_damage = FALSE;

 done_ipp_still:

    if (dst_buf)
        exynosUtilVideoBufferUnref(dst_buf);
    if (ui_buf)
        exynosUtilVideoBufferUnref(ui_buf);
    if (pix_buf)
        exynosUtilVideoBufferUnref(pix_buf);

 convert_ipp_still:

    return Success;
}

static int
_exynosVirtualVideoCompositeExtLayers(EXYNOSPortPrivPtr pPort,
                                      int connector_type)
{
    EXYNOSVideoBuf *dst_buf = NULL;
    EXYNOSLayer *lower_layer = NULL;
    EXYNOSLayer *upper_layer = NULL;
    EXYNOSVideoBuf *ui_buf = NULL;
    xRectangle rect = { 0, };
    int index;
    Bool comp = FALSE;

    if (!_exynosVirtualVideoEnsureOutBuffers
        (pPort->pScrn, pPort, pPort->id, pPort->pDraw->width,
         pPort->pDraw->height))
        return FALSE;

    index = _exynosVirtualVideoGetOutBufferIndex(pPort);
    if (index < 0) {
        XDBG_WARNING(MVA, "all out buffers are in use.\n");
        return FALSE;
    }

    lower_layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (lower_layer) {
        EXYNOSVideoBuf *lower_buf = exynosLayerGetBuffer(lower_layer);

        XDBG_RETURN_VAL_IF_FAIL(lower_buf != NULL, FALSE);

        if (lower_buf && !lower_buf->exynosure && VBUF_IS_VALID(lower_buf)) {
            /* In case of virtual, lower layer already has full-size. */
            dst_buf = lower_buf;
            comp = TRUE;
        }
    }

    if (!dst_buf) {
        if (!_exynosVirtualVideoEnsureDstBuffer(pPort))
            return FALSE;

        dst_buf = pPort->dstbuf;
    }

    /* before compositing, flush all */
    exynosUtilCacheFlush(pPort->pScrn);

    comp = TRUE;                //if set to FALSE capture may be black. something with layers?

    ui_buf = _exynosVirtualVideoGetUIBuffer(pPort, connector_type);     //Need to choose active connector DRM_MODE_CONNECTOR_VIRTUAL
    if (ui_buf) {
        XDBG_DEBUG(MVA,
                   "ui : %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                   FOURCC_STR(ui_buf->id), ui_buf->width, ui_buf->height,
                   ui_buf->crop.x, ui_buf->crop.y, ui_buf->crop.width,
                   ui_buf->crop.height, FOURCC_STR(dst_buf->id), dst_buf->width,
                   dst_buf->height, 0, 0, dst_buf->width, dst_buf->height);

        if (!_exynosVirtualVideoComposite(ui_buf, dst_buf,
                                          ui_buf->crop.x, ui_buf->crop.y,
                                          ui_buf->crop.width,
                                          ui_buf->crop.height, 0, 0,
                                          dst_buf->width, dst_buf->height, comp,
                                          0)) {
            exynosUtilVideoBufferUnref(ui_buf);
            return FALSE;
        }

//        comp = TRUE;
    }

    upper_layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_UPPER);
    if (upper_layer) {
        EXYNOSVideoBuf *upper_buf = exynosLayerGetBuffer(upper_layer);

        if (upper_buf && VBUF_IS_VALID(upper_buf)) {
            exynosLayerGetRect(upper_layer, &upper_buf->crop, &rect);

            XDBG_DEBUG(MVA,
                       "upper : %c%c%c%c  %dx%d (%d,%d %dx%d) => dst : %c%c%c%c  %dx%d (%d,%d %dx%d)\n",
                       FOURCC_STR(upper_buf->id), upper_buf->width,
                       upper_buf->height, upper_buf->crop.x, upper_buf->crop.y,
                       upper_buf->crop.width, upper_buf->crop.height,
                       FOURCC_STR(dst_buf->id), dst_buf->width, dst_buf->height,
                       rect.x, rect.y, rect.width, rect.height);

            _exynosVirtualVideoComposite(upper_buf, dst_buf,
                                         upper_buf->crop.x, upper_buf->crop.y,
                                         upper_buf->crop.width,
                                         upper_buf->crop.height, rect.x, rect.y,
                                         rect.width, rect.height, comp, 0);
        }
    }

    dst_buf->crop.x = 0;
    dst_buf->crop.y = 0;
    dst_buf->crop.width = dst_buf->width;
    dst_buf->crop.height = dst_buf->height;

    XDBG_RETURN_VAL_IF_FAIL(pPort->outbuf[index] != NULL, FALSE);

    pPort->outbuf[index]->crop.x = 0;
    pPort->outbuf[index]->crop.y = 0;
    pPort->outbuf[index]->crop.width = pPort->outbuf[index]->width;
    pPort->outbuf[index]->crop.height = pPort->outbuf[index]->height;
    _exynosVirtualVideoComposite(dst_buf, pPort->outbuf[index],
                                 0, 0, dst_buf->width, dst_buf->height,
                                 0, 0, pPort->outbuf[index]->width,
                                 pPort->outbuf[index]->height, FALSE, 0);

    _exynosVirtualVideoDraw(pPort, pPort->outbuf[index]);

    if (ui_buf)
        exynosUtilVideoBufferUnref(ui_buf);

    return TRUE;
}

static void
_exynosVirtualVideoCompositeSubtitle(EXYNOSPortPrivPtr pPort,
                                     EXYNOSVideoBuf * vbuf)
{
    EXYNOSLayer *subtitle_layer;
    EXYNOSVideoBuf *subtitle_vbuf;
    xRectangle src_rect;
    xRectangle dst_rect;

    subtitle_layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_UPPER);
    if (!subtitle_layer)
        return;

    subtitle_vbuf = exynosLayerGetBuffer(subtitle_layer);
    if (!subtitle_vbuf || !VBUF_IS_VALID(subtitle_vbuf))
        return;

    CLEAR(src_rect);
    CLEAR(dst_rect);
    exynosLayerGetRect(subtitle_layer, &src_rect, &dst_rect);

    XDBG_DEBUG(MVA, "subtitle : %dx%d (%d,%d %dx%d) => %dx%d (%d,%d %dx%d)\n",
               subtitle_vbuf->width, subtitle_vbuf->height,
               src_rect.x, src_rect.y, src_rect.width, src_rect.height,
               vbuf->width, vbuf->height,
               dst_rect.x, dst_rect.y, dst_rect.width, dst_rect.height);

    _exynosVirtualVideoComposite(subtitle_vbuf, vbuf,
                                 src_rect.x, src_rect.y, src_rect.width,
                                 src_rect.height, dst_rect.x, dst_rect.y,
                                 dst_rect.width, dst_rect.height, TRUE, 0);
}

static CARD32
_exynosVirtualVideoRetireTimeout(OsTimerPtr timer, CARD32 now, pointer arg)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) arg;
    EXYNOSModePtr pExynosMode;
    int diff;

    if (!pPort)
        return 0;

    pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pPort->pScrn)->pExynosMode;

    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    XDBG_ERROR(MVA, "capture(%d) mode(%d) conn(%d) type(%d) status(%x). \n",
               pPort->capture, pExynosMode->set_mode, pExynosMode->conn_mode,
               pPort->data_type, pPort->status);

    diff = GetTimeInMillis() - pPort->retire_time;
    XDBG_ERROR(MVA, "failed : +++ Retire Timeout!! diff(%d)\n", diff);

    return 0;
}

static void
_exynosVirtualVideoLayerNotifyFunc(EXYNOSLayer * layer, int type,
                                   void *type_data, void *data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    EXYNOSVideoBuf *vbuf = (EXYNOSVideoBuf *) type_data;
    EXYNOSVideoBuf *black;

    exynosLayerRemoveNotifyFunc(layer, _exynosVirtualVideoLayerNotifyFunc);

    if (type == LAYER_DESTROYED || type != LAYER_BUF_CHANGED || !vbuf)
        goto fail_layer_noti;

    XDBG_GOTO_IF_FAIL(VBUF_IS_VALID(vbuf), fail_layer_noti);
    XDBG_GOTO_IF_FAIL(vbuf->showing == FALSE, fail_layer_noti);

    XDBG_DEBUG(MVA, "------------------------------\n");

    _exynosVirtualVideoCompositeSubtitle(pPort, vbuf);
    _exynosVirtualVideoDraw(pPort, vbuf);
    XDBG_DEBUG(MVA, "------------------------------...\n");

    return;

 fail_layer_noti:
    black = _exynosVirtualVideoGetBlackBuffer(pPort);
    XDBG_TRACE(MVA, "black buffer(%d) return: layer noti. type(%d), vbuf(%p)\n",
               (black) ? black->keys[0] : 0, type, vbuf);
    _exynosVirtualVideoDraw(pPort, black);
}

static int
_exynosVirtualVideoPutStill(EXYNOSPortPrivPtr pPort, int connector_type)
{
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pPort->pScrn)->pExynosMode;
    EXYNOSVideoBuf *pix_buf = NULL;
    EXYNOSVideoBuf *ui_buf = NULL;
    Bool comp;
    int i;
    CARD32 start = GetTimeInMillis();

    TTRACE_VIDEO_BEGIN("XORG:XV:VIDEOPUTSTILL");

    XDBG_GOTO_IF_FAIL(pPort->exynosure == FALSE, done_still);

    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    comp = TRUE;                //if set to FALSE capture will be black. something with layers?

    pix_buf = _exynosVirtualVideoGetDrawableBuffer(pPort);
    XDBG_GOTO_IF_FAIL(pix_buf != NULL, done_still);

    ui_buf = _exynosVirtualVideoGetUIBuffer(pPort, connector_type);
    XDBG_GOTO_IF_FAIL(ui_buf != NULL, done_still);

    tbm_bo_map(pix_buf->bo[0], TBM_DEVICE_2D, TBM_OPTION_WRITE);

    for (i = LAYER_LOWER2; i < LAYER_MAX; i++) {
        EXYNOSVideoBuf *upper = NULL;
        xRectangle src_rect, dst_rect;
        int vwidth = pExynosMode->main_lcd_mode.hdisplay;
        int vheight = pExynosMode->main_lcd_mode.vdisplay;
        int rotate;

        if (i == LAYER_DEFAULT) {
            upper = exynosUtilVideoBufferRef(ui_buf);
            tbm_bo_map(upper->bo[0], TBM_DEVICE_2D, TBM_OPTION_READ);

            src_rect.x = src_rect.y = 0;
            src_rect.width = ui_buf->width;
            src_rect.height = ui_buf->height;

            dst_rect.x = dst_rect.y = 0;
            dst_rect.width = ui_buf->width;
            dst_rect.height = ui_buf->height;

            rotate = 0;
        }
        else {
            EXYNOSLayer *layer =
                exynosLayerFind(LAYER_OUTPUT_LCD, (EXYNOSLayerPos) i);
            int off_x = 0, off_y = 0;
            EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

            if (!layer)
                continue;

            upper = exynosUtilVideoBufferRef(exynosLayerGetBuffer(layer));
            if (!upper || !VBUF_IS_VALID(upper))
                continue;

            exynosLayerGetRect(layer, &src_rect, &dst_rect);
            exynosLayerGetOffset(layer, &off_x, &off_y);
            dst_rect.x += off_x;
            dst_rect.y += off_y;

            rotate = (360 - pVideo->screen_rotate_degree) % 360;

            /* rotate upper_rect */
            exynosUtilRotateArea(&vwidth, &vheight, &dst_rect, rotate);
        }

        /* scale upper_rect */
        exynosUtilScaleRect(vwidth, vheight, pix_buf->width, pix_buf->height,
                            &dst_rect);

        XDBG_DEBUG(MVA,
                   "%dx%d(%d,%d, %dx%d) => %dx%d(%d,%d, %dx%d) :comp(%d) r(%d)\n",
                   upper->width, upper->height, src_rect.x, src_rect.y,
                   src_rect.width, src_rect.height, pix_buf->width,
                   pix_buf->height, dst_rect.x, dst_rect.y, dst_rect.width,
                   dst_rect.height, comp, rotate);

        if (!_exynosVirtualVideoComposite(upper, pix_buf,
                                          src_rect.x, src_rect.y,
                                          src_rect.width, src_rect.height,
                                          dst_rect.x, dst_rect.y,
                                          dst_rect.width, dst_rect.height,
                                          comp, rotate)) {
            if (i == LAYER_DEFAULT)
                tbm_bo_unmap(upper->bo[0]);
            tbm_bo_unmap(pix_buf->bo[0]);
            goto done_still;
        }

        if (i == LAYER_DEFAULT)
            tbm_bo_unmap(upper->bo[0]);

        exynosUtilVideoBufferUnref(upper);

        comp = TRUE;
    }

    XDBG_TRACE(MVA, "make still: %ldms\n", GetTimeInMillis() - start);

    tbm_bo_unmap(pix_buf->bo[0]);

 done_still:

    exynosUtilCacheFlush(pPort->pScrn);

    if (pix_buf)
        exynosUtilVideoBufferUnref(pix_buf);
    if (ui_buf)
        exynosUtilVideoBufferUnref(ui_buf);

    DamageDamageRegion(pPort->pDraw, pPort->clipBoxes);
    pPort->need_damage = FALSE;

    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if ((pExynos->dump_mode & XBERC_DUMP_MODE_CA) && pExynos->dump_info) {
        PixmapPtr pPixmap = _exynosVirtualVideoGetPixmap(pPort->pDraw);
        char file[128];
        static int i;

        snprintf(file, sizeof(file), "capout_still_%03d.bmp", i++);
        exynosUtilDoDumpPixmaps(pExynos->dump_info, pPixmap, file,
                                DUMP_TYPE_BMP);
    }

    TTRACE_VIDEO_END();

    return Success;
}

static int
_exynosVirtualVideoPutWB(EXYNOSPortPrivPtr pPort)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    XDBG_RETURN_VAL_IF_FAIL(pPort->pScrn != NULL, BadImplementation);
    XDBG_RETURN_VAL_IF_FAIL(pPort->pDraw != NULL, BadImplementation);

    if (!pPort->wb) {
        if (!exynosWbIsOpened()) {
            int scanout;

            if (!_exynosVirtualVideoEnsureOutBuffers
                (pPort->pScrn, pPort, pPort->id, pPort->pDraw->width,
                 pPort->pDraw->height))
                return BadAlloc;
            if (pPort->display == DISPLAY_MAIN)
                scanout = FALSE;
            else
                scanout = pExynos->scanout;

            /* For capture mode, we don't need to create contiguous buffer.
             * Rotation should be considered when wb begins.
             */
            pPort->wb = exynosWbOpen(pPort->pScrn, pPort->id,
                                     pPort->pDraw->width, pPort->pDraw->height,
                                     scanout, 60,
                                     (pPort->rotate_off) ? FALSE : TRUE);
            XDBG_RETURN_VAL_IF_FAIL(pPort->wb != NULL, BadAlloc);
            exynosWbSetBuffer(pPort->wb, pPort->outbuf, pPort->outbuf_num);

            XDBG_TRACE(MVA, "wb(%p) start. \n", pPort->wb);

            if (!exynosWbStart(pPort->wb)) {
                exynosWbClose(pPort->wb);
                pPort->wb = NULL;
                return BadAlloc;
            }
            pPort->need_close_wb = TRUE;
        }
        else {
            pPort->wb = exynosWbGet();
            pPort->need_close_wb = FALSE;
        }
        if (pPort->capture == CAPTURE_MODE_STILL) {
            exynosWbAddNotifyFunc(pPort->wb, WB_NOTI_IPP_EVENT_DONE,
                                  _exynosVirtualVideoWbDumpDoneFunc, pPort);
        }

        exynosWbAddNotifyFunc(pPort->wb, WB_NOTI_STOP,
                              _exynosVirtualVideoWbStopFunc, pPort);
        exynosWbAddNotifyFunc(pPort->wb, WB_NOTI_IPP_EVENT,
                              _exynosVirtualVideoWbDumpFunc, pPort);

        exynosWbAddNotifyFunc(pPort->wb, WB_NOTI_CLOSED,
                              _exynosVirtualVideoWbCloseFunc, pPort);
    }

    /* no available buffer, need to return buffer by client. */
    if (!exynosWbIsRunning()) {
        XDBG_WARNING(MVA, "wb is stopped.\n");
        return BadRequest;
    }
#if 0
    /* no available buffer, need to return buffer by client. */
    if (!exynosWbCanDequeueBuffer(pPort->wb)) {
        XDBG_TRACE(MVA, "no available buffer\n");
        return BadRequest;
    }
#endif
    XDBG_TRACE(MVA, "wb(%p), running(%d). \n", pPort->wb, exynosWbIsRunning());

    return Success;
}

static int
_exynosVirtualVideoPutVideoOnly(EXYNOSPortPrivPtr pPort)
{
    EXYNOSLayer *layer;
    EXYNOSVideoBuf *vbuf;
    int i;

    XDBG_RETURN_VAL_IF_FAIL(pPort->display == DISPLAY_EXTERNAL, BadRequest);
    XDBG_RETURN_VAL_IF_FAIL(pPort->capture == CAPTURE_MODE_STREAM, BadRequest);

    TTRACE_VIDEO_BEGIN("XORG:XV:PUTVIDEOONLY");

    layer = exynosLayerFind(LAYER_OUTPUT_EXT, LAYER_LOWER1);
    if (!layer) {
        TTRACE_VIDEO_END();
        return BadRequest;
    }

    if (!pPort->outbuf) {
        if (!_exynosVirtualVideoEnsureOutBuffers
            (pPort->pScrn, pPort, pPort->id, pPort->pDraw->width,
             pPort->pDraw->height)) {
            XDBG_ERROR(MVA, "Didn't create outbuf array\n");
            TTRACE_VIDEO_END();
            return BadAlloc;
        }
    }

    for (i = 0; i < pPort->outbuf_num; i++) {
        if (!pPort->outbuf[i]) {
            XDBG_ERROR(MVA, "Didn't create outbuf %d\n", i);
            TTRACE_VIDEO_END();
            return BadRequest;
        }

        if (!pPort->outbuf[i]->showing)
            break;
    }

    if (i == pPort->outbuf_num) {
        XDBG_ERROR(MVA, "now all buffers are in showing\n");
        TTRACE_VIDEO_END();
        return BadRequest;
    }

    vbuf = exynosLayerGetBuffer(layer);
    /* if layer is just created, vbuf can't be null. */
    if (!vbuf || !VBUF_IS_VALID(vbuf)) {
        EXYNOSVideoBuf *black = _exynosVirtualVideoGetBlackBuffer(pPort);

        XDBG_GOTO_IF_FAIL(black != NULL, put_fail);

        XDBG_TRACE(MVA, "black buffer(%d) return: vbuf invalid\n",
                   black->keys[0]);
        _exynosVirtualVideoDraw(pPort, black);
        TTRACE_VIDEO_END();
        return Success;
    }

    /* Wait the next frame if it's same as previous one */
    if (_exynosVirtualVideoFindReturnBuf(pPort, vbuf->keys[0])) {
        exynosLayerAddNotifyFunc(layer, _exynosVirtualVideoLayerNotifyFunc,
                                 pPort);
        XDBG_DEBUG(MVA, "wait notify.\n");
        TTRACE_VIDEO_END();
        return Success;
    }

    _exynosVirtualVideoCompositeSubtitle(pPort, vbuf);
    _exynosVirtualVideoDraw(pPort, vbuf);

    TTRACE_VIDEO_END();

    return Success;

 put_fail:
    TTRACE_VIDEO_END();
    return BadRequest;
}

static int
_exynosVirtualVideoPutExt(EXYNOSPortPrivPtr pPort, int active_connector)
{
    if (_exynosVirtualVideoCompositeExtLayers(pPort, active_connector))
        return Success;

    return BadRequest;
}

static int
EXYNOSVirtualVideoGetPortAttribute(ScrnInfoPtr pScrn,
                                   Atom attribute, INT32 *value, pointer data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (attribute == _exynosVideoGetPortAtom(PAA_FORMAT)) {
        *value = pPort->id;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_CAPTURE)) {
        *value = pPort->capture;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_DISPLAY)) {
        *value = pPort->display;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_ROTATE_OFF)) {
        *value = pPort->rotate_off;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_DATA_TYPE)) {
        *value = pPort->data_type;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_SECURE)) {
        *value = pPort->exynosure;
        return Success;
    }
    return BadMatch;
}

static int
EXYNOSVirtualVideoSetPortAttribute(ScrnInfoPtr pScrn,
                                   Atom attribute, INT32 value, pointer data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (attribute == _exynosVideoGetPortAtom(PAA_FORMAT)) {
        if (!_exynosVirtualVideoIsSupport((unsigned int) value)) {
            XDBG_ERROR(MVA, "id(%c%c%c%c) not supported.\n", FOURCC_STR(value));
            return BadRequest;
        }

        pPort->id = (unsigned int) value;
        XDBG_DEBUG(MVA, "id(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_CAPTURE)) {
        if (value < CAPTURE_MODE_NONE || value >= CAPTURE_MODE_MAX) {
            XDBG_ERROR(MVA, "capture value(%d) is out of range\n", (int) value);
            return BadRequest;
        }

        pPort->capture = value;
        XDBG_DEBUG(MVA, "capture(%d) \n", pPort->capture);
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_DISPLAY)) {
        pPort->display = value;
        XDBG_DEBUG(MVA, "display: %d \n", pPort->display);
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_ROTATE_OFF)) {
        XDBG_DEBUG(MVA, "ROTATE_OFF: %d! \n", pPort->rotate_off);
        pPort->rotate_off = value;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_SECURE)) {
        XDBG_TRACE(MVA, "not implemented 'exynosure' attr. (%d) \n",
                   pPort->exynosure);
//        pPort->exynosure = value;
        return Success;
    }
    else if (attribute == _exynosVideoGetPortAtom(PAA_RETBUF)) {
        RetBufInfo *info;

        if (!pPort->pDraw)
            return Success;

        info = _exynosVirtualVideoFindReturnBuf(pPort, value);
        if (!info) {
            XDBG_WARNING(MVA, "wrong gem name(%d) returned\n", (int) value);
            return Success;
        }

        if (info->vbuf && info->vbuf->need_reset)
            exynosUtilClearVideoBuffer(info->vbuf);

        _exynosVirtualVideoRemoveReturnBuf(pPort, info);
#if 0
        _buffers(pPort);
        ErrorF("[Xorg] retbuf : %ld (%s)\n", value, buffers);
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
EXYNOSVirtualVideoQueryBestSize(ScrnInfoPtr pScrn,
                                Bool motion,
                                short vid_w, short vid_h,
                                short dst_w, short dst_h,
                                unsigned int *p_w, unsigned int *p_h,
                                pointer data)
{
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (pPort->display == DISPLAY_EXTERNAL) {
        if (p_w)
            *p_w = pExynosMode->ext_connector_mode.hdisplay;
        if (p_h)
            *p_h = pExynosMode->ext_connector_mode.vdisplay;
    }
    else {
        if (p_w)
            *p_w = (unsigned int) dst_w;
        if (p_h)
            *p_h = (unsigned int) dst_h;
    }
}

/* vid_x, vid_y, vid_w, vid_h : no meaning for us. not using.
 * drw_x, drw_y, dst_w, dst_h : no meaning for us. not using.
 * Only pDraw's size is used.
 */
static int
EXYNOSVirtualVideoPutStill(ScrnInfoPtr pScrn,
                           short vid_x, short vid_y, short drw_x, short drw_y,
                           short vid_w, short vid_h, short drw_w, short drw_h,
                           RegionPtr clipBoxes, pointer data, DrawablePtr pDraw)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    int ret = BadRequest;

    XDBG_GOTO_IF_FAIL(pPort->need_damage == FALSE, put_still_fail);

#if 0
    ErrorF("[Xorg] PutStill\n");
#endif

    TTRACE_VIDEO_BEGIN("XORG:XV:PUTSTILL");

    XDBG_DEBUG(MVA, "*************************************** \n");

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_CA) {
        CARD32 cur, sub;

        cur = GetTimeInMillis();
        sub = cur - pPort->prev_time;
        pPort->prev_time = cur;
        ErrorF("getstill interval     : %6" PRIXID " ms\n", sub);
    }

    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    if (pExynos->pVideoPriv->no_retbuf)
        _exynosVirtualVideoRemoveReturnBufAll(pPort);

    pPort->retire_timer = TimerSet(pPort->retire_timer, 0, 4000,
                                   _exynosVirtualVideoRetireTimeout, pPort);
    XDBG_GOTO_IF_FAIL(pPort->id > 0, put_still_fail);

    pPort->status = 0;
    pPort->retire_time = GetTimeInMillis();

    ret = _exynosVirtualVideoPreProcess(pScrn, pPort, clipBoxes, pDraw);
    XDBG_GOTO_IF_FAIL(ret == Success, put_still_fail);

    ret = _exynosVirtualVideoAddDrawableEvent(pPort);
    XDBG_GOTO_IF_FAIL(ret == Success, put_still_fail);

#if 0
    /* check drawable */
    XDBG_RETURN_VAL_IF_FAIL(pDraw->type == DRAWABLE_PIXMAP, BadPixmap);
#endif

    if (!pPort->putstill_on) {
        pPort->putstill_on = TRUE;
        XDBG_SECURE(MVA,
                    "pPort(%d) putstill on. exynosure(%d), capture(%d), display(%d) format(%c%c%c%c)\n",
                    pPort->index, pPort->exynosure, pPort->capture,
                    pPort->display, FOURCC_STR(pPort->id));
    }

    pPort->need_damage = TRUE;

    /*find what connector is active */
    int active_connector = -1;  //DRM_MODE_CONNECTOR_HDMIA

    active_connector = findActiveConnector(pPort->pScrn);

    if (active_connector == -1) {
        ret = BadRequest;
        goto put_still_fail;
    }
    pPort->active_connector = active_connector;

    if (pPort->capture == CAPTURE_MODE_STILL && pPort->display == DISPLAY_MAIN) {
        if (pPort->active_connector == DRM_MODE_CONNECTOR_LVDS) {
            XDBG_DEBUG(MVA, "still mode LCD.\n");

            ret = _exynosVirtualVideoPutStill(pPort, active_connector);
            XDBG_GOTO_IF_FAIL(ret == Success, put_still_fail);
        }
        else {
            XDBG_DEBUG(MVA, "still mode HDMI or Virtual Display.\n");
            ret =
                _exynosVirtualStillCompositeExtLayers(pPort, active_connector,
                                                      FALSE);
            XDBG_GOTO_IF_FAIL(ret == Success, put_still_fail);
        }
    }
    else if (pPort->capture == CAPTURE_MODE_STREAM &&
             pPort->display == DISPLAY_MAIN) {
        XDBG_DEBUG(MVA, "stream mode.\n");
        if (EXYNOSPTR(pScrn)->isLcdOff) {
            XDBG_TRACE(MVA, "DPMS status: off. \n");
            ret = BadRequest;
            goto put_still_fail;
        }

        if (0)
            ret = _exynosVirtualVideoPutWB(pPort);
        else
            ret = _exynosVirtualVideoPutExt(pPort, active_connector);
        if (ret != Success)
            goto put_still_fail;
    }
    else if (pPort->capture == CAPTURE_MODE_STREAM &&
             pPort->display == DISPLAY_EXTERNAL) {
        int old_data_type = pPort->data_type;
        EXYNOSVideoBuf *black;

        switch (pExynosMode->set_mode) {
        case DISPLAY_SET_MODE_OFF:
            XDBG_DEBUG(MVA, "display mode is off. \n");
            black = _exynosVirtualVideoGetBlackBuffer(pPort);
            if (black == NULL) {
                ret = BadRequest;
                goto put_still_fail;
            }
            XDBG_DEBUG(MVA, "black buffer(%d) return: lcd off\n",
                       black->keys[0]);
            _exynosVirtualVideoDraw(pPort, black);
            ret = Success;
            goto put_still_fail;
#if 0
        case DISPLAY_SET_MODE_CLONE:
            pPort->data_type = _exynosVirtualVideoDataType(pPort);

            if (pPort->data_type != old_data_type)
                _exynosVirtualVideoSendPortNotify(pPort, PAA_DATA_TYPE,
                                                  pPort->data_type);

            if (pPort->data_type == DATA_TYPE_UI) {
                XDBG_DEBUG(MVA, "clone mode.\n");

                ret = _exynosVirtualVideoPutWB(pPort);
                if (ret != Success)
                    goto put_still_fail;
            }
            else {
                XDBG_DEBUG(MVA, "video only mode.\n");
                ret = _exynosVirtualVideoPutVideoOnly(pPort);
                if (ret != Success)
                    goto put_still_fail;
            }
            break;

        case DISPLAY_SET_MODE_EXT:
            XDBG_DEBUG(MVA, "desktop mode.\n");

            if (pExynosMode->ext_connector_mode.hdisplay != pDraw->width ||
                pExynosMode->ext_connector_mode.vdisplay != pDraw->height) {
                XDBG_ERROR(MVA,
                           "drawble should have %dx%d size. mode(%d), conn(%d)\n",
                           pExynosMode->ext_connector_mode.hdisplay,
                           pExynosMode->ext_connector_mode.vdisplay,
                           pExynosMode->set_mode, pExynosMode->conn_mode);
                ret = BadRequest;
                goto put_still_fail;
            }

            ret = _exynosVirtualVideoPutExt(pPort, active_connector);
            if (ret != Success)
                goto put_still_fail;
            break;
#else
        case DISPLAY_SET_MODE_EXT:
            pPort->data_type = _exynosVirtualVideoDataType(pPort);

            if (pPort->data_type != old_data_type)
                _exynosVirtualVideoSendPortNotify(pPort, PAA_DATA_TYPE,
                                                  pPort->data_type);
            if (pPort->data_type == DATA_TYPE_NONE) {
                XDBG_DEBUG(MVA, "enable clone mode.\n");

                ret = _exynosVirtualVideoPutWB(pPort);

                if (ret != Success)
                    goto put_still_fail;
            }
            else if (pPort->data_type == DATA_TYPE_UI || pPort->need_close_wb) {
                XDBG_DEBUG(MVA, "clone mode.\n");

                ret = _exynosVirtualVideoPutWB(pPort);

                if (ret != Success)
                    goto put_still_fail;
            }
            else if (pPort->data_type == DATA_TYPE_VIDEO &&
                     !pPort->need_close_wb) {
                XDBG_DEBUG(MVA, "video only mode.\n");

                ret = _exynosVirtualVideoPutVideoOnly(pPort);

                if (ret != Success)
                    goto put_still_fail;
            }

            break;
#endif
        default:
            break;
        }
    }
    else {
        XDBG_NEVER_GET_HERE(MVA);
        ret = BadRequest;
        goto put_still_fail;
    }

    XDBG_DEBUG(MVA, "***************************************.. \n");

    TTRACE_VIDEO_END();

    return Success;

 put_still_fail:
    pPort->need_damage = FALSE;

    if (pPort->retire_timer) {
        TimerFree(pPort->retire_timer);
        pPort->retire_timer = NULL;
    }

    XDBG_DEBUG(MVA, "***************************************.. \n");

    TTRACE_VIDEO_END();

    return ret;
}

static void
EXYNOSVirtualVideoStop(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    _exynosVirtualVideoStreamOff(pPort);
}

static int
EXYNOSVirtualVideoDDPutStill(ClientPtr client,
                             DrawablePtr pDraw,
                             XvPortPtr pPort,
                             GCPtr pGC,
                             INT16 vid_x, INT16 vid_y,
                             CARD16 vid_w, CARD16 vid_h,
                             INT16 drw_x, INT16 drw_y, CARD16 drw_w,
                             CARD16 drw_h)
{
    EXYNOSVideoPortInfo *info = _port_info(pDraw);
    int ret;

    if (info) {
        info->client = client;
        info->pp = pPort;
    }

    ret = ddPutStill(client, pDraw, pPort, pGC,
                     vid_x, vid_y, vid_w, vid_h, drw_x, drw_y, drw_w, drw_h);

    return ret;
}

XF86VideoAdaptorPtr
exynosVideoSetupVirtualVideo(ScreenPtr pScreen)
{
    XF86VideoAdaptorPtr pAdaptor;
    EXYNOSPortPrivPtr pPort;
    int i;

    pAdaptor = calloc(1, sizeof(XF86VideoAdaptorRec) +
                      (sizeof(DevUnion) +
                       sizeof(EXYNOSPortPriv)) * EXYNOS_MAX_PORT);
    if (!pAdaptor)
        return NULL;

    dummy_encoding[0].width = pScreen->width;
    dummy_encoding[0].height = pScreen->height;

    pAdaptor->type = XvWindowMask | XvPixmapMask | XvInputMask | XvStillMask;
    pAdaptor->flags = 0;
    pAdaptor->name = "EXYNOS Virtual Video";
    pAdaptor->nEncodings =
        sizeof(dummy_encoding) / sizeof(XF86VideoEncodingRec);
    pAdaptor->pEncodings = dummy_encoding;
    pAdaptor->nFormats = NUM_FORMATS;
    pAdaptor->pFormats = formats;
    pAdaptor->nPorts = EXYNOS_MAX_PORT;
    pAdaptor->pPortPrivates = (DevUnion *) (&pAdaptor[1]);

    pPort = (EXYNOSPortPrivPtr) (&pAdaptor->pPortPrivates[EXYNOS_MAX_PORT]);

    for (i = 0; i < EXYNOS_MAX_PORT; i++) {
        pAdaptor->pPortPrivates[i].ptr = &pPort[i];
        pPort[i].index = i;
        pPort[i].id = FOURCC_RGB32;
        pPort[i].outbuf_index = -1;

        xorg_list_init(&pPort[i].retbuf_info);
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;
    pAdaptor->nImages = NUM_IMAGES;
    pAdaptor->pImages = images;

    pAdaptor->GetPortAttribute = EXYNOSVirtualVideoGetPortAttribute;
    pAdaptor->SetPortAttribute = EXYNOSVirtualVideoSetPortAttribute;
    pAdaptor->QueryBestSize = EXYNOSVirtualVideoQueryBestSize;
    pAdaptor->PutStill = EXYNOSVirtualVideoPutStill;
    pAdaptor->StopVideo = EXYNOSVirtualVideoStop;

    if (!_exynosVirtualVideoRegisterEventResourceTypes()) {
        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];

        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to register EventResourceTypes. \n");
        return NULL;
    }

    return pAdaptor;
}

void
exynosVirtualVideoDpms(ScrnInfoPtr pScrn, Bool on)
{
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pExynos->pVideoPriv->pAdaptor[1];
    int i;

    if (on)
        return;

    for (i = 0; i < EXYNOS_MAX_PORT; i++) {
        EXYNOSPortPrivPtr pPort =
            (EXYNOSPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->wb) {
            exynosWbClose(pPort->wb);
            pPort->wb = NULL;
        }
    }
}

void
exynosVirtualVideoReplacePutStillFunc(ScreenPtr pScreen)
{
    int i;

    XvScreenPtr xvsp = dixLookupPrivate(&pScreen->devPrivates,
                                        XvGetScreenKey());

    if (!xvsp)
        return;

    for (i = 1; i < xvsp->nAdaptors; i++) {
        XvAdaptorPtr pAdapt = xvsp->pAdaptors + i;

        if (pAdapt->ddPutStill) {
            ddPutStill = pAdapt->ddPutStill;
            pAdapt->ddPutStill = EXYNOSVirtualVideoDDPutStill;
            break;
        }
    }

    if (!dixRegisterPrivateKey
        (VideoVirtualPortKey, PRIVATE_WINDOW, sizeof(EXYNOSVideoPortInfo)))
        return;
    if (!dixRegisterPrivateKey
        (VideoVirtualPortKey, PRIVATE_PIXMAP, sizeof(EXYNOSVideoPortInfo)))
        return;
}

void
exynosVirtualVideoGetBuffers(ScrnInfoPtr pScrn, int id, int width, int height,
                             EXYNOSVideoBuf *** vbufs, int *bufnum)
{
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pExynos->pVideoPriv->pAdaptor[1];
    int i;

    for (i = 0; i < EXYNOS_MAX_PORT; i++) {
        EXYNOSPortPrivPtr pPort =
            (EXYNOSPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->pDraw) {
            XDBG_RETURN_IF_FAIL(pPort->id == id);
            XDBG_RETURN_IF_FAIL(pPort->pDraw->width == width);
            XDBG_RETURN_IF_FAIL(pPort->pDraw->height == height);
        }

        if (!_exynosVirtualVideoEnsureOutBuffers
            (pScrn, pPort, id, width, height))
            return;

        *vbufs = pPort->outbuf;
        *bufnum = pPort->outbuf_num;
    }
}

static void
_exynosCaptureCvtCallback(EXYNOSCvt * cvt,
                          EXYNOSVideoBuf * src,
                          EXYNOSVideoBuf * dst, void *cvt_data, Bool error)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) cvt_data;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    XDBG_RETURN_IF_FAIL(cvt != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(src));
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(dst));

    XDBG_DEBUG(MVA, "++++++++++++++++++++++++ \n");
    XDBG_DEBUG(MVA, "cvt(%p) src(%p) dst(%p)\n", cvt, src, dst);

    pPort->capture_dstbuf = dst;

    /*begin composition again */
    _exynosVirtualStillCompositeExtLayers(pPort, pPort->active_connector, TRUE);
}

static void
_exynosCaptureEnsureConverter(EXYNOSPortPrivPtr pPort)
{
    if (pPort->cvt2)
        return;

    pPort->cvt2 = exynosCvtCreate(pPort->pScrn, CVT_OP_M2M);
    XDBG_RETURN_IF_FAIL(pPort->cvt2 != NULL);

    exynosCvtAddCallback(pPort->cvt2, _exynosCaptureCvtCallback, pPort);
}

static void
_exynosCaptureCloseConverter(EXYNOSPortPrivPtr pPort)
{
    if (pPort->cvt2) {
        exynosCvtDestroy(pPort->cvt2);
        pPort->cvt2 = NULL;
    }

    XDBG_TRACE(MVA, "done. \n");
}

static Bool
exynosCaptureConvertImage(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * inbuf,
                          int csc_range)
{
    EXYNOSCvtProp src_prop = { 0, }, dst_prop = {
    0,};
    EXYNOSVideoBuf *outbuf = NULL;

    pPort->exynosure = 0;

    src_prop.id = inbuf->id;
    src_prop.width = inbuf->width;
    src_prop.height = inbuf->height;
    src_prop.crop = inbuf->crop;

    dst_prop.id = FOURCC_RGB32;
    dst_prop.width = inbuf->width;
    dst_prop.height = inbuf->height;
    dst_prop.crop = inbuf->crop;

    dst_prop.degree = 0;
    dst_prop.hflip = 0;
    dst_prop.vflip = 0;
    dst_prop.exynosure = pPort->exynosure;
    dst_prop.csc_range = 0;     // pPort->csc_range;

    if (!exynosCvtEnsureSize(&src_prop, &dst_prop))
        goto fail_to_convert;

    XDBG_GOTO_IF_FAIL(pPort != NULL, fail_to_convert);

    outbuf = pPort->capture_dstbuf;
    if (outbuf == NULL) {
        outbuf = exynosUtilAllocVideoBuffer(pPort->pScrn, FOURCC_RGB32,
                                            dst_prop.width, dst_prop.height,
                                            FALSE, FALSE, pPort->exynosure);
        XDBG_GOTO_IF_FAIL(outbuf != NULL, fail_to_convert);

        outbuf->crop = dst_prop.crop;
    }

    _exynosCaptureEnsureConverter(pPort);
    XDBG_GOTO_IF_FAIL(pPort->cvt2 != NULL, fail_to_convert);

    if (!exynosCvtSetProperpty(pPort->cvt2, &src_prop, &dst_prop))
        goto fail_to_convert;

    if (!exynosCvtConvert(pPort->cvt2, inbuf, outbuf))
        goto fail_to_convert;

    exynosUtilVideoBufferUnref(outbuf);

    return TRUE;

 fail_to_convert:

    if (outbuf)
        exynosUtilVideoBufferUnref(outbuf);

    _exynosCaptureCloseConverter(pPort);

    return FALSE;
}
