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

#include <string.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/ioctl.h>

#include <pixman.h>
#include <X11/Xatom.h>
#include <X11/extensions/Xv.h>
#include <X11/extensions/Xvproto.h>
#include <fourcc.h>

#include <fb.h>
#include <fbdevhw.h>
#include <damage.h>

#include <xf86xv.h>

#include "sec.h"

#include "sec_accel.h"
#include "sec_display.h"
#include "sec_crtc.h"
#include "sec_output.h"
#include "sec_video.h"
#include "sec_prop.h"
#include "sec_util.h"
#include "sec_wb.h"
#include "sec_video_virtual.h"
#include "sec_video_display.h"
#include "sec_video_tvout.h"
#include "sec_video_fourcc.h"
#include "sec_converter.h"
#include "sec_plane.h"
#include "sec_xberc.h"

#include "xv_types.h"

#include <exynos_drm.h>

#define DONT_FILL_ALPHA     -1
#define SEC_MAX_PORT        16

#define INBUF_NUM           6
#define OUTBUF_NUM          3
#define NUM_HW_LAYER        2

#define OUTPUT_LCD   (1 << 0)
#define OUTPUT_EXT   (1 << 1)
#define OUTPUT_FULL  (1 << 8)

static XF86VideoEncodingRec dummy_encoding[] =
{
    { 0, "XV_IMAGE", -1, -1, { 1, 1 } },
    { 1, "XV_IMAGE", 4224, 4224, { 1, 1 } },
};

static XF86ImageRec images[] =
{
    XVIMAGE_YUY2,
    XVIMAGE_SUYV,
    XVIMAGE_UYVY,
    XVIMAGE_SYVY,
    XVIMAGE_ITLV,
    XVIMAGE_YV12,
    XVIMAGE_I420,
    XVIMAGE_S420,
    XVIMAGE_ST12,
    XVIMAGE_NV12,
    XVIMAGE_SN12,
    XVIMAGE_NV21,
    XVIMAGE_SN21,
    XVIMAGE_RGB32,
    XVIMAGE_SR32,
    XVIMAGE_RGB565,
    XVIMAGE_SR16,
};

static XF86VideoFormatRec formats[] =
{
    { 16, TrueColor },
    { 24, TrueColor },
    { 32, TrueColor },
};

static XF86AttributeRec attributes[] =
{
    { 0, 0, 270, "_USER_WM_PORT_ATTRIBUTE_ROTATION" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_HFLIP" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_VFLIP" },
    { 0, -1, 1, "_USER_WM_PORT_ATTRIBUTE_PREEMPTION" },
    { 0, 0, OUTPUT_MODE_EXT_ONLY, "_USER_WM_PORT_ATTRIBUTE_OUTPUT" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_SECURE" },
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_CSC_RANGE" },
};

typedef enum
{
    PAA_MIN,
    PAA_ROTATION,
    PAA_HFLIP,
    PAA_VFLIP,
    PAA_PREEMPTION,
    PAA_OUTPUT,
    PAA_SECURE,
    PAA_CSC_RANGE,
    PAA_MAX
} SECPortAttrAtom;

static struct
{
    SECPortAttrAtom  paa;
    const char      *name;
    Atom             atom;
} atoms[] =
{
    { PAA_ROTATION, "_USER_WM_PORT_ATTRIBUTE_ROTATION", None },
    { PAA_HFLIP, "_USER_WM_PORT_ATTRIBUTE_HFLIP", None },
    { PAA_VFLIP, "_USER_WM_PORT_ATTRIBUTE_VFLIP", None },
    { PAA_PREEMPTION, "_USER_WM_PORT_ATTRIBUTE_PREEMPTION", None },
    { PAA_OUTPUT, "_USER_WM_PORT_ATTRIBUTE_OUTPUT", None },
    { PAA_SECURE, "_USER_WM_PORT_ATTRIBUTE_SECURE", None },
    { PAA_CSC_RANGE, "_USER_WM_PORT_ATTRIBUTE_CSC_RANGE", None },
};

enum
{
    ON_NONE,
    ON_FB,
    ON_WINDOW,
    ON_PIXMAP
};

static char *drawing_type[4] = {"NONE", "FB", "WIN", "PIX"};

typedef struct _PutData
{
    unsigned int id;
    int          width;
    int          height;
    xRectangle   src;
    xRectangle   dst;
    void        *buf;
    Bool         sync;
    RegionPtr    clip_boxes;
    void        *data;
    DrawablePtr  pDraw;
} PutData;

/* SEC port information structure */
typedef struct
{
    CARD32 prev_time;
    int index;

    /* attributes */
    int rotate;
    int hflip;
    int vflip;
    int preemption;     /* 1:high, 0:default, -1:low */
    Bool secure;
    int csc_range;

    ScrnInfoPtr pScrn;
    PutData d;
    PutData old_d;

    /* draw inform */
    int          drawing;
    int          hw_rotate;

    int           in_width;
    int           in_height;
    xRectangle    in_crop;
    SECVideoBuf  *inbuf[INBUF_NUM];
    Bool          inbuf_is_fb;

    /* converter */
    SECCvt       *cvt;

    /* layer */
    SECLayer     *layer;
    int           out_width;
    int           out_height;
    xRectangle    out_crop;
    SECVideoBuf  *outbuf[OUTBUF_NUM];
    int           outbuf_cvting;
    DrawablePtr   pDamageDrawable[OUTBUF_NUM];

    /* tvout */
    int usr_output;
    int old_output;
    int grab_tvout;
    SECVideoTv *tv;
    void       *gem_list;
    Bool skip_tvout;
    Bool need_start_wb;
    SECVideoBuf  *wait_vbuf;
    CARD32 tv_prev_time;

    /* count */
    unsigned int put_counts;
    OsTimerPtr timer;

    Bool punched;
    int  stream_cnt;
    struct xorg_list link;
} SECPortPriv, *SECPortPrivPtr;

static RESTYPE event_drawable_type;

typedef struct _SECVideoResource
{
    XID id;
    RESTYPE type;

    SECPortPrivPtr pPort;
    ScrnInfoPtr pScrn;
} SECVideoResource;

typedef struct _SECVideoPortInfo
{
    ClientPtr client;
    XvPortPtr pp;
} SECVideoPortInfo;

static int (*ddPutImage) (ClientPtr, DrawablePtr, struct _XvPortRec *, GCPtr,
                          INT16, INT16, CARD16, CARD16,
                          INT16, INT16, CARD16, CARD16,
                          XvImagePtr, unsigned char *, Bool, CARD16, CARD16);

static void _secVideoSendReturnBufferMessage (SECPortPrivPtr pPort, SECVideoBuf *vbuf, unsigned int *keys);
static void SECVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit);
static void _secVideoCloseInBuffer (SECPortPrivPtr pPort);
static void _secVideoCloseOutBuffer (SECPortPrivPtr pPort, Bool close_layer);
static void _secVideoCloseConverter (SECPortPrivPtr pPort);
static Bool _secVideoSetOutputExternalProperty (DrawablePtr pDraw, Bool tvout);

static int streaming_ports;
static int registered_handler;
static struct xorg_list layer_owners;

static DevPrivateKeyRec video_port_key;
#define VideoPortKey (&video_port_key)
#define GetPortInfo(pDraw) ((SECVideoPortInfo*)dixLookupPrivate(&(pDraw)->devPrivates, VideoPortKey))

#define NUM_IMAGES        (sizeof(images) / sizeof(images[0]))
#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

#define ENSURE_AREA(off, lng, max) (lng = ((off + lng) > max ? (max - off) : lng))

static CARD32
_countPrint (OsTimerPtr timer, CARD32 now, pointer arg)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)arg;

    if (pPort->timer)
    {
        TimerFree (pPort->timer);
        pPort->timer = NULL;
    }

    ErrorF ("PutImage(%d) : %d fps. \n", pPort->index, pPort->put_counts);

    pPort->put_counts = 0;

    return 0;
}

static void
_countFps (SECPortPrivPtr pPort)
{
    pPort->put_counts++;

    if (pPort->timer)
        return;

    pPort->timer = TimerSet (NULL, 0, 1000, _countPrint, pPort);
}

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

static PixmapPtr
_getPixmap (DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pDraw->pScreen->GetWindowPixmap ((WindowPtr) pDraw);
    else
        return (PixmapPtr) pDraw;
}

static XF86ImagePtr
_get_image_info (int id)
{
    int i;

    for (i = 0; i < NUM_IMAGES; i++)
        if (images[i].id == id)
            return &images[i];

    return NULL;
}

static Atom
_portAtom (SECPortAttrAtom paa)
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

    XDBG_ERROR (MVDO, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static void
_DestroyData (void *port, void *data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)port;
    unsigned int handle = (unsigned int)data;

    secUtilFreeHandle (pPort->pScrn, handle);
}

static Bool
_secVideoGrabTvout (SECPortPrivPtr pPort)
{
    SECVideoPrivPtr pVideo = SECPTR(pPort->pScrn)->pVideoPriv;

    if (pPort->grab_tvout)
        return TRUE;

    /* other port already grabbed */
    if (pVideo->tvout_in_use)
    {
        XDBG_WARNING (MVDO, "*** pPort(%p) can't grab tvout. It's in use.\n", pPort);
        return FALSE;
    }

    if (pPort->tv)
    {
        XDBG_ERROR (MVDO, "*** wrong handle if you reach here. %p \n", pPort->tv);
        return FALSE;
    }

    pPort->grab_tvout = TRUE;
    pVideo->tvout_in_use = TRUE;

    XDBG_TRACE (MVDO, "pPort(%p) grabs tvout.\n", pPort);

    return TRUE;
}

static void
_secVideoUngrabTvout (SECPortPrivPtr pPort)
{
    if (pPort->tv)
    {
        secVideoTvDisconnect (pPort->tv);
        pPort->tv = NULL;
    }

    /* This port didn't grab tvout */
    if (!pPort->grab_tvout)
        return;

    _secVideoSetOutputExternalProperty (pPort->d.pDraw, FALSE);

    if (pPort->need_start_wb)
    {
        SECWb *wb = secWbGet ();
        if (wb)
        {
            secWbSetSecure (wb, pPort->secure);
            secWbStart (wb);
        }
        pPort->need_start_wb = FALSE;
    }

    XDBG_TRACE (MVDO, "pPort(%p) ungrabs tvout.\n", pPort);

    pPort->grab_tvout = FALSE;

    if (pPort->pScrn)
    {
        SECVideoPrivPtr pVideo;
        pVideo = SECPTR(pPort->pScrn)->pVideoPriv;
        pVideo->tvout_in_use = FALSE;
    }
    pPort->wait_vbuf = NULL;
}

static int
_secVideoGetTvoutMode (SECPortPrivPtr pPort)
{
    SECModePtr pSecMode = (SECModePtr) SECPTR (pPort->pScrn)->pSecMode;
    SECVideoPrivPtr pVideo = SECPTR(pPort->pScrn)->pVideoPriv;
    SECDisplaySetMode disp_mode = secDisplayGetDispSetMode (pPort->pScrn);
    int output = OUTPUT_LCD;

    if (disp_mode == DISPLAY_SET_MODE_CLONE)
    {
        if (pPort->preemption > -1)
        {
            if (pVideo->video_output > 0 && streaming_ports == 1)
            {
                int video_output = pVideo->video_output - 1;

                if (video_output == OUTPUT_MODE_DEFAULT)
                    output = OUTPUT_LCD;
                else if (video_output == OUTPUT_MODE_TVOUT)
                    output = OUTPUT_LCD|OUTPUT_EXT|OUTPUT_FULL;
                else
                    output = OUTPUT_EXT|OUTPUT_FULL;
            }
            else if (streaming_ports == 1)
            {
                output = pPort->usr_output;
                if (!(output & OUTPUT_FULL))
                    output &= ~(OUTPUT_EXT);
            }
            else if (streaming_ports > 1)
                output = OUTPUT_LCD;
            else
                XDBG_NEVER_GET_HERE (MVDO);
        }
        else
            output = OUTPUT_LCD;
    }
    else if (disp_mode == DISPLAY_SET_MODE_EXT)
    {
        if (pPort->drawing == ON_PIXMAP)
            output = OUTPUT_LCD;
        else
        {
            xf86CrtcPtr pCrtc = secCrtcGetAtGeometry (pPort->pScrn,
                          (int)pPort->d.pDraw->x, (int)pPort->d.pDraw->y,
                          (int)pPort->d.pDraw->width, (int)pPort->d.pDraw->height);
            int c = secCrtcGetConnectType (pCrtc);

            if (c == DRM_MODE_CONNECTOR_LVDS || c == DRM_MODE_CONNECTOR_Unknown)
                output = OUTPUT_LCD;
            else if (c == DRM_MODE_CONNECTOR_HDMIA || c == DRM_MODE_CONNECTOR_HDMIB)
                output = OUTPUT_EXT;
            else if (c == DRM_MODE_CONNECTOR_VIRTUAL)
                output = OUTPUT_EXT;
            else
                XDBG_NEVER_GET_HERE (MVDO);
        }
    }
    else /* DISPLAY_SET_MODE_OFF */
    {
        output = OUTPUT_LCD;
    }

    if (pPort->drawing == ON_PIXMAP)
        output = OUTPUT_LCD;

    XDBG_DEBUG (MVDO, "drawing(%d) disp_mode(%d) preemption(%d) streaming_ports(%d) conn_mode(%d) usr_output(%d) video_output(%d) output(%x) skip(%d)\n",
                pPort->drawing, disp_mode, pPort->preemption, streaming_ports, pSecMode->conn_mode,
                pPort->usr_output, pVideo->video_output, output, pPort->skip_tvout);

    return output;
}

static int
_secVideodrawingOn (SECPortPrivPtr pPort)
{
    if (pPort->old_d.pDraw != pPort->d.pDraw)
        pPort->drawing = ON_NONE;

    if (pPort->drawing != ON_NONE)
        return pPort->drawing;

    if (pPort->d.pDraw->type == DRAWABLE_PIXMAP)
        return ON_PIXMAP;
    else if (pPort->d.pDraw->type == DRAWABLE_WINDOW)
    {
        PropertyPtr prop = secUtilGetWindowProperty ((WindowPtr)pPort->d.pDraw,
                                                     "XV_ON_DRAWABLE");
        if (prop && *(int*)prop->data > 0)
            return ON_WINDOW;
    }

    return ON_FB;
}

static void
_secVideoGetRotation (SECPortPrivPtr pPort, int *hw_rotate)
{
    SECVideoPrivPtr pVideo = SECPTR(pPort->pScrn)->pVideoPriv;

    /*
     * RR_Rotate_90:  Target turns to 90. UI turns to 270.
     * RR_Rotate_270: Target turns to 270. UI turns to 90.
     *
     *     [Target]            ----------
     *                         |        |
     *     Top (RR_Rotate_90)  |        |  Top (RR_Rotate_270)
     *                         |        |
     *                         ----------
     *     [UI,FIMC]           ----------
     *                         |        |
     *      Top (degree: 270)  |        |  Top (degree: 90)
     *                         |        |
     *                         ----------
     */

    if (pPort->drawing == ON_FB)
        *hw_rotate = (pPort->rotate + pVideo->screen_rotate_degree) % 360;
    else
        *hw_rotate = pPort->rotate % 360;
}

static int
_secVideoGetKeys (SECPortPrivPtr pPort, unsigned int *keys, unsigned int *type)
{
    XV_DATA_PTR data = (XV_DATA_PTR) pPort->d.buf;
    int valid = XV_VALIDATE_DATA (data);

    if (valid == XV_HEADER_ERROR)
    {
        XDBG_ERROR (MVDO, "XV_HEADER_ERROR\n");
        return valid;
    }
    else if (valid == XV_VERSION_MISMATCH)
    {
        XDBG_ERROR (MVDO, "XV_VERSION_MISMATCH\n");
        return valid;
    }

    if (keys)
    {
        keys[0] = data->YBuf;
        keys[1] = data->CbBuf;
        keys[2] = data->CrBuf;
    }

    if (type)
        *type = data->BufType;

    return 0;
}

static void
_secVideoFreeInbuf (SECVideoBuf *vbuf, void *data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)data;
    int i;

    XDBG_RETURN_IF_FAIL (pPort->drawing != ON_NONE);

    for (i = 0; i < INBUF_NUM; i++)
        if (pPort->inbuf[i] == vbuf)
        {
            _secVideoSendReturnBufferMessage (pPort, vbuf, NULL);
            pPort->inbuf[i] = NULL;
            return;
        }

    XDBG_NEVER_GET_HERE (MVDO);
}

static void
_secVideoFreeOutbuf (SECVideoBuf *vbuf, void *data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)data;
    int i;

    XDBG_RETURN_IF_FAIL (pPort->drawing != ON_NONE);

    for (i = 0; i < OUTBUF_NUM; i++)
        if (pPort->outbuf[i] == vbuf)
        {
            pPort->pDamageDrawable[i] = NULL;
            pPort->outbuf[i] = NULL;
            return;
        }

    XDBG_NEVER_GET_HERE (MVDO);
}

static SECLayer*
_secVideoCreateLayer (SECPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    SECVideoPrivPtr pVideo = SECPTR(pScrn)->pVideoPriv;
    DrawablePtr pDraw = pPort->d.pDraw;
    xf86CrtcConfigPtr pCrtcConfig;
    xf86OutputPtr pOutput = NULL;
    int i;
    xf86CrtcPtr pCrtc;
    SECLayer   *layer;
    Bool full = TRUE;
    xRectangle src, dst;

    pCrtc = secCrtcGetAtGeometry (pScrn, pDraw->x, pDraw->y, pDraw->width, pDraw->height);
    XDBG_RETURN_VAL_IF_FAIL (pCrtc != NULL, NULL);

    pCrtcConfig = XF86_CRTC_CONFIG_PTR (pCrtc->scrn);
    XDBG_RETURN_VAL_IF_FAIL (pCrtcConfig != NULL, NULL);

    for (i = 0; i < pCrtcConfig->num_output; i++)
    {
        xf86OutputPtr pTemp = pCrtcConfig->output[i];
        if (pTemp->crtc == pCrtc)
        {
            pOutput = pTemp;
            break;
        }
    }
    XDBG_RETURN_VAL_IF_FAIL (pOutput != NULL, NULL);

    SECOutputPrivPtr pOutputPriv = pOutput->driver_private;
    SECLayerOutput output = LAYER_OUTPUT_LCD;

    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_Unknown)
    {
        output = LAYER_OUTPUT_LCD;
    }
    else if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIA ||
             pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_HDMIB ||
             pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_VIRTUAL)
    {
        output = LAYER_OUTPUT_EXT;
    }
    else
        XDBG_NEVER_GET_HERE (MVDO);

    if (!secLayerFind (output, LAYER_LOWER2) || !secLayerFind (output, LAYER_LOWER1))
        full = FALSE;

    if (full)
        return NULL;

    layer = secLayerCreate (pScrn, output, LAYER_NONE);
    XDBG_RETURN_VAL_IF_FAIL (layer != NULL, NULL);

    src = dst = pPort->out_crop;
    dst.x = pPort->d.dst.x;
    dst.y = pPort->d.dst.y;

    secLayerSetRect (layer, &src, &dst);
    secLayerEnableVBlank (layer, TRUE);

    xorg_list_add (&pPort->link, &layer_owners);

    secLayerSetOffset (layer, pVideo->video_offset_x, pVideo->video_offset_y);

    return layer;
}

static SECVideoBuf*
_secVideoGetInbufZeroCopy (SECPortPrivPtr pPort, unsigned int *names, unsigned int buf_type)
{
    SECVideoBuf *inbuf = NULL;
    int i, empty;
    tbm_bo_handle bo_handle;

    for (empty = 0; empty < INBUF_NUM; empty++)
        if (!pPort->inbuf[empty])
            break;

    if (empty == INBUF_NUM)
    {
        XDBG_ERROR (MVDO, "now all inbufs in use!\n");
        return NULL;
    }

    /* make sure both widths are same.*/
    XDBG_RETURN_VAL_IF_FAIL (pPort->d.width == pPort->in_width, NULL);
    XDBG_RETURN_VAL_IF_FAIL (pPort->d.height == pPort->in_height, NULL);

    inbuf = secUtilCreateVideoBuffer (pPort->pScrn, pPort->d.id,
                                      pPort->in_width, pPort->in_height,
                                      pPort->secure);
    XDBG_RETURN_VAL_IF_FAIL (inbuf != NULL, NULL);

    inbuf->crop = pPort->in_crop;

    for (i = 0; i < PLANAR_CNT; i++)
    {
        if (names[i] > 0)
        {
            inbuf->keys[i] = names[i];

            if (buf_type == XV_BUF_TYPE_LEGACY)
            {
                void *data = secUtilListGetData (pPort->gem_list, (void*)names[i]);
                if (!data)
                {
                    secUtilConvertPhyaddress (pPort->pScrn, names[i], inbuf->lengths[i], &inbuf->handles[i]);

                    pPort->gem_list = secUtilListAdd (pPort->gem_list, (void*)names[i],
                                                      (void*)inbuf->handles[i]);
                }
                else
                    inbuf->handles[i] = (unsigned int)data;

                XDBG_DEBUG (MVDO, "%d, %p => %d \n", i, (void*)names[i], inbuf->handles[i]);
            }
            else
            {
                XDBG_GOTO_IF_FAIL (inbuf->lengths[i] > 0, fail_dma);
                XDBG_GOTO_IF_FAIL (inbuf->bo[i] == NULL, fail_dma);

                inbuf->bo[i] = tbm_bo_import (SECPTR (pPort->pScrn)->tbm_bufmgr, inbuf->keys[i]);
                XDBG_GOTO_IF_FAIL (inbuf->bo[i] != NULL, fail_dma);

                bo_handle = tbm_bo_get_handle(inbuf->bo[i], TBM_DEVICE_DEFAULT);
                inbuf->handles[i] = bo_handle.u32;
                XDBG_GOTO_IF_FAIL (inbuf->handles[i] > 0, fail_dma);

                XDBG_DEBUG (MVDO, "%d, key(%d) => bo(%p) handle(%d)\n",
                            i, inbuf->keys[i], inbuf->bo[i], inbuf->handles[i]);
            }
        }
    }

    /* not increase ref_cnt to free inbuf when converting/showing is done. */
    pPort->inbuf[empty] = inbuf;

    secUtilAddFreeVideoBufferFunc (inbuf, _secVideoFreeInbuf, pPort);

    return inbuf;

fail_dma:
    if (inbuf)
        secUtilFreeVideoBuffer (inbuf);

    return NULL;
}

static SECVideoBuf*
_secVideoGetInbufRAW (SECPortPrivPtr pPort)
{
    SECVideoBuf *inbuf = NULL;
    void *vir_addr = NULL;
    int i;
    tbm_bo_handle bo_handle;

    /* we can't access virtual pointer. */
    XDBG_RETURN_VAL_IF_FAIL (pPort->secure == FALSE, NULL);

    for (i = 0; i < INBUF_NUM; i++)
    {
        if (pPort->inbuf[i])
            continue;

        pPort->inbuf[i] = secUtilAllocVideoBuffer (pPort->pScrn, pPort->d.id,
                                                   pPort->in_width, pPort->in_height,
                                                   FALSE, FALSE, pPort->secure);
        XDBG_GOTO_IF_FAIL (pPort->inbuf[i] != NULL, fail_raw_alloc);
    }

    for (i = 0; i < INBUF_NUM; i++)
    {
        XDBG_DEBUG (MVDO, "? inbuf(%d,%p) converting(%d) showing(%d)\n", i,
                    pPort->inbuf[i], VBUF_IS_CONVERTING (pPort->inbuf[i]), pPort->inbuf[i]->showing);

        if (pPort->inbuf[i] && !VBUF_IS_CONVERTING (pPort->inbuf[i]) && !pPort->inbuf[i]->showing)
        {
            /* increase ref_cnt to keep inbuf until stream_off. */
            inbuf = secUtilVideoBufferRef (pPort->inbuf[i]);
            break;
        }
    }

    if (!inbuf)
    {
        XDBG_ERROR (MVDO, "now all inbufs in use!\n");
        return NULL;
    }

    inbuf->crop = pPort->in_crop;

    bo_handle = tbm_bo_map (inbuf->bo[0], TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    vir_addr = bo_handle.ptr;
    XDBG_RETURN_VAL_IF_FAIL (vir_addr != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL (inbuf->size > 0, NULL);

    if (pPort->d.width != pPort->in_width || pPort->d.height != pPort->in_height)
    {
        XF86ImagePtr image_info = _get_image_info (pPort->d.id);
        XDBG_RETURN_VAL_IF_FAIL (image_info != NULL, NULL);
        int pitches[3] = {0,};
        int offsets[3] = {0,};
        int lengths[3] = {0,};
        int width, height;

        width = pPort->d.width;
        height = pPort->d.height;

        secVideoQueryImageAttrs (pPort->pScrn, pPort->d.id,
                                 &width, &height,
                                 pitches, offsets, lengths);

        secUtilCopyImage (width, height,
                          pPort->d.buf, width, height,
                          pitches, offsets, lengths,
                          vir_addr, inbuf->width, inbuf->height,
                          inbuf->pitches, inbuf->offsets, inbuf->lengths,
                          image_info->num_planes,
                          image_info->horz_u_period,
                          image_info->vert_u_period);
    }
    else
        memcpy (vir_addr, pPort->d.buf, inbuf->size);

    tbm_bo_unmap (inbuf->bo[0]);
    secUtilCacheFlush (pPort->pScrn);
    return inbuf;

fail_raw_alloc:
    _secVideoCloseInBuffer (pPort);
    return NULL;
}

static SECVideoBuf*
_secVideoGetInbuf (SECPortPrivPtr pPort)
{
    unsigned int keys[PLANAR_CNT] = {0,};
    unsigned int buf_type = 0;
    SECVideoBuf *inbuf = NULL;
    SECPtr pSec = SECPTR (pPort->pScrn);

    if (IS_ZEROCOPY (pPort->d.id))
    {
        if (_secVideoGetKeys (pPort, keys, &buf_type))
            return NULL;

        XDBG_RETURN_VAL_IF_FAIL (keys[0] > 0, NULL);

        if (pPort->d.id == FOURCC_SN12 || pPort->d.id == FOURCC_ST12)
            XDBG_RETURN_VAL_IF_FAIL (keys[1] > 0, NULL);

        inbuf = _secVideoGetInbufZeroCopy (pPort, keys, buf_type);

        XDBG_RETURN_VAL_IF_FAIL (inbuf != NULL, NULL);
        XDBG_TRACE (MVDO, "keys: %d,%d,%d. stamp(%ld)\n", keys[0], keys[1], keys[2], inbuf->stamp);
    }
    else
        inbuf = _secVideoGetInbufRAW (pPort);

    XDBG_RETURN_VAL_IF_FAIL (inbuf != NULL, NULL);

    if ((pSec->dump_mode & XBERC_DUMP_MODE_IA) && pSec->dump_info)
    {
        char file[128];
        static int i;
        snprintf (file, sizeof(file), "xvin_%c%c%c%c_%dx%d_p%d_%03d.%s",
                  FOURCC_STR(inbuf->id),
                  inbuf->width, inbuf->height, pPort->index, i++,
                  IS_RGB(inbuf->id)?"bmp":"yuv");
        secUtilDoDumpVBuf (pSec->dump_info, inbuf, file);
    }

    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_IA)
        inbuf->put_time = GetTimeInMillis ();

    return inbuf;
}

static SECVideoBuf*
_secVideoGetOutbufDrawable (SECPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    DrawablePtr pDraw = pPort->d.pDraw;
    PixmapPtr pPixmap = (PixmapPtr)_getPixmap (pDraw);
    SECPixmapPriv *privPixmap = exaGetPixmapDriverPrivate (pPixmap);
    Bool need_finish = FALSE;
    SECVideoBuf *outbuf = NULL;
    int empty;
    tbm_bo_handle bo_handle;

    for (empty = 0; empty < OUTBUF_NUM; empty++)
        if (!pPort->outbuf[empty])
            break;

    if (empty == OUTBUF_NUM)
    {
        XDBG_ERROR (MVDO, "now all outbufs in use!\n");
        return NULL;
    }

    if ((pDraw->width % 16) && (pPixmap->usage_hint != CREATE_PIXMAP_USAGE_XVIDEO))
    {
        ScreenPtr pScreen = pScrn->pScreen;
        SECFbBoDataPtr bo_data = NULL;

        pPixmap->usage_hint = CREATE_PIXMAP_USAGE_XVIDEO;
        pScreen->ModifyPixmapHeader (pPixmap,
                                     pDraw->width, pDraw->height,
                                     pDraw->depth,
                                     pDraw->bitsPerPixel,
                                     pPixmap->devKind, 0);
        XDBG_RETURN_VAL_IF_FAIL (privPixmap->bo != NULL, NULL);

        tbm_bo_get_user_data(privPixmap->bo, TBM_BO_DATA_FB, (void**)&bo_data);
        XDBG_RETURN_VAL_IF_FAIL (bo_data != NULL, NULL);
        XDBG_RETURN_VAL_IF_FAIL ((bo_data->pos.x2 - bo_data->pos.x1) == pPort->out_width, NULL);
        XDBG_RETURN_VAL_IF_FAIL ((bo_data->pos.y2 - bo_data->pos.y1) == pPort->out_height, NULL);
    }

    if (!privPixmap->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL (privPixmap->bo != NULL, fail_drawable);
    }

    outbuf = secUtilCreateVideoBuffer (pScrn, FOURCC_RGB32,
                                       pPort->out_width,
                                       pPort->out_height,
                                       pPort->secure);
    XDBG_GOTO_IF_FAIL (outbuf != NULL, fail_drawable);
    outbuf->crop = pPort->out_crop;

    XDBG_TRACE (MVDO, "outbuf(%p)(%dx%d) created. [%s]\n",
                outbuf, pPort->out_width, pPort->out_height,
                (pPort->drawing==ON_PIXMAP)?"PIX":"WIN");

    outbuf->bo[0] = tbm_bo_ref (privPixmap->bo);

    bo_handle = tbm_bo_get_handle (outbuf->bo[0], TBM_DEVICE_DEFAULT);
    outbuf->handles[0] = bo_handle.u32;
    XDBG_GOTO_IF_FAIL (outbuf->handles[0] > 0, fail_drawable);

    if (need_finish)
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

    pPort->pDamageDrawable[empty] = pPort->d.pDraw;

//    RegionTranslate (pPort->d.clip_boxes, -pPort->d.dst.x, -pPort->d.dst.y);

    /* not increase ref_cnt to free outbuf when converting/showing is done. */
    pPort->outbuf[empty] = outbuf;

    secUtilAddFreeVideoBufferFunc (outbuf, _secVideoFreeOutbuf, pPort);

    return outbuf;

fail_drawable:
    if (outbuf)
        secUtilFreeVideoBuffer (outbuf);

    return NULL;
}

static SECVideoBuf*
_secVideoGetOutbufFB (SECPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    SECVideoBuf *outbuf = NULL;
    int i, next;

    if (!pPort->layer)
    {
        pPort->layer = _secVideoCreateLayer (pPort);
        XDBG_RETURN_VAL_IF_FAIL (pPort->layer != NULL, NULL);
    }
    else
    {
        SECVideoBuf *vbuf = secLayerGetBuffer (pPort->layer);
        if (vbuf && (vbuf->width == pPort->out_width && vbuf->height == pPort->out_height))
        {
            xRectangle src = {0,}, dst = {0,};

            secLayerGetRect (pPort->layer, &src, &dst);

            /* CHECK */
            if (pPort->d.dst.x != dst.x || pPort->d.dst.y != dst.y)
            {
                /* x,y can be changed when window is moved. */
                dst.x = pPort->d.dst.x;
                dst.y = pPort->d.dst.y;
                secLayerSetRect (pPort->layer, &src, &dst);
            }
        }
    }

    for (i = 0; i < OUTBUF_NUM; i++)
    {
        SECPtr pSec = SECPTR (pPort->pScrn);

        if (pPort->outbuf[i])
            continue;

        pPort->outbuf[i] = secUtilAllocVideoBuffer (pScrn, FOURCC_RGB32,
                                                    pPort->out_width, pPort->out_height,
                                                    (pSec->scanout)?TRUE:FALSE,
                                                    FALSE, pPort->secure);
        XDBG_GOTO_IF_FAIL (pPort->outbuf[i] != NULL, fail_fb);
        pPort->outbuf[i]->crop = pPort->out_crop;

        XDBG_TRACE (MVDO, "out bo(%p, %d, %dx%d) created. [FB]\n",
                    pPort->outbuf[i]->bo[0], pPort->outbuf[i]->handles[0],
                    pPort->out_width, pPort->out_height);
    }

    next = ++pPort->outbuf_cvting;
    if (next >= OUTBUF_NUM)
        next = 0;

    for (i = 0; i < OUTBUF_NUM; i++)
    {
        XDBG_DEBUG (MVDO, "? outbuf(%d,%p) converting(%d)\n", next,
                    pPort->outbuf[next], VBUF_IS_CONVERTING (pPort->outbuf[next]));

        if (pPort->outbuf[next] && !VBUF_IS_CONVERTING (pPort->outbuf[next]))
        {
            /* increase ref_cnt to keep outbuf until stream_off. */
            outbuf = secUtilVideoBufferRef (pPort->outbuf[next]);
            break;
        }

        next++;
        if (next >= OUTBUF_NUM)
            next = 0;
    }

    if (!outbuf)
    {
        XDBG_ERROR (MVDO, "now all outbufs in use!\n");
        return NULL;
    }

    pPort->outbuf_cvting = next;

    return outbuf;

fail_fb:
    _secVideoCloseConverter (pPort);
    _secVideoCloseOutBuffer (pPort, TRUE);

    return NULL;
}

static SECVideoBuf*
_secVideoGetOutbuf (SECPortPrivPtr pPort)
{
    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        return _secVideoGetOutbufDrawable (pPort);
    else /* ON_FB */
        return _secVideoGetOutbufFB (pPort);
}

static void
_secVideoCloseInBuffer (SECPortPrivPtr pPort)
{
    int i;

    _secVideoUngrabTvout (pPort);

    if (pPort->gem_list)
    {
        secUtilListDestroyData (pPort->gem_list, _DestroyData, pPort);
        secUtilListDestroy (pPort->gem_list);
        pPort->gem_list = NULL;
    }

    if (!IS_ZEROCOPY (pPort->d.id))
        for (i = 0; i < INBUF_NUM; i++)
        {
            if (pPort->inbuf[i])
            {
                secUtilVideoBufferUnref (pPort->inbuf[i]);
                pPort->inbuf[i] = NULL;
            }
        }

    pPort->in_width = 0;
    pPort->in_height = 0;
    memset (&pPort->in_crop, 0, sizeof (xRectangle));

    XDBG_DEBUG (MVDO, "done\n");
}

static void
_secVideoCloseOutBuffer (SECPortPrivPtr pPort, Bool close_layer)
{
    int i;

    /* before close outbuf, layer/cvt should be finished. */
    if (close_layer && pPort->layer)
    {
        secLayerUnref (pPort->layer);
        pPort->layer = NULL;
        xorg_list_del (&pPort->link);
    }

    for (i = 0; i < OUTBUF_NUM; i++)
    {
        if (pPort->outbuf[i])
        {
            if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
                XDBG_NEVER_GET_HERE (MVDO);

            secUtilVideoBufferUnref (pPort->outbuf[i]);
            pPort->outbuf[i] = NULL;
        }
    }

    pPort->out_width = 0;
    pPort->out_height = 0;
    memset (&pPort->out_crop, 0, sizeof (xRectangle));
    pPort->outbuf_cvting = -1;

    XDBG_DEBUG (MVDO, "done\n");
}

static void
_secVideoSendReturnBufferMessage (SECPortPrivPtr pPort, SECVideoBuf *vbuf, unsigned int *keys)
{
    static Atom return_atom = None;
    SECVideoPortInfo *info = _port_info (pPort->d.pDraw);

    if (return_atom == None)
        return_atom = MakeAtom ("XV_RETURN_BUFFER",
                                strlen ("XV_RETURN_BUFFER"), TRUE);

    if (!info)
        return;

    xEvent event;

    CLEAR (event);
    event.u.u.type = ClientMessage;
    event.u.u.detail = 32;
    event.u.clientMessage.u.l.type = return_atom;
    if (vbuf)
    {
        event.u.clientMessage.u.l.longs0 = (INT32)vbuf->keys[0];
        event.u.clientMessage.u.l.longs1 = (INT32)vbuf->keys[1];
        event.u.clientMessage.u.l.longs2 = (INT32)vbuf->keys[2];

        XDBG_TRACE (MVDO, "%ld: %d,%d,%d out. diff(%ld)\n", vbuf->stamp,
                    vbuf->keys[0], vbuf->keys[1], vbuf->keys[2], GetTimeInMillis()-vbuf->stamp);
    }
    else if (keys)
    {
        event.u.clientMessage.u.l.longs0 = (INT32)keys[0];
        event.u.clientMessage.u.l.longs1 = (INT32)keys[1];
        event.u.clientMessage.u.l.longs2 = (INT32)keys[2];

        XDBG_TRACE (MVDO, "%d,%d,%d out. \n",
                    keys[0], keys[1], keys[2]);
    }
    else
        XDBG_NEVER_GET_HERE (MVDO);

    WriteEventsToClient(info->client, 1, (xEventPtr) &event);

    SECPtr pSec = SECPTR (pPort->pScrn);
    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_IA)
    {
        if (vbuf)
        {
            CARD32 cur, sub;
            cur = GetTimeInMillis ();
            sub = cur - vbuf->put_time;
            ErrorF ("vbuf(%d,%d,%d)         retbuf  : %6ld ms\n",
                    vbuf->keys[0], vbuf->keys[1], vbuf->keys[2], sub);
        }
        else if (keys)
            ErrorF ("vbuf(%d,%d,%d)         retbuf  : 0 ms\n",
                    keys[0], keys[1], keys[2]);
        else
            XDBG_NEVER_GET_HERE (MVDO);
    }
}

static void
_secVideoCvtCallback (SECCvt *cvt,
                      SECVideoBuf *src,
                      SECVideoBuf *dst,
                      void *cvt_data,
                      Bool error)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)cvt_data;
    DrawablePtr pDamageDrawable = NULL;
    int out_index;

    XDBG_RETURN_IF_FAIL (pPort != NULL);
    XDBG_RETURN_IF_FAIL (cvt != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (src));
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (dst));
    XDBG_DEBUG (MVDO, "++++++++++++++++++++++++ \n");
    XDBG_DEBUG (MVDO, "cvt(%p) src(%p) dst(%p)\n", cvt, src, dst);

    for (out_index = 0; out_index < OUTBUF_NUM; out_index++)
        if (pPort->outbuf[out_index] == dst)
            break;
    XDBG_RETURN_IF_FAIL (out_index < OUTBUF_NUM);

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        pDamageDrawable = pPort->pDamageDrawable[out_index];
    else
        pDamageDrawable = pPort->d.pDraw;

    XDBG_RETURN_IF_FAIL (pDamageDrawable != NULL);

    if (error)
    {
        DamageDamageRegion (pDamageDrawable, pPort->d.clip_boxes);
        return;
    }

    SECPtr pSec = SECPTR (pPort->pScrn);
    if ((pSec->dump_mode & XBERC_DUMP_MODE_IA) && pSec->dump_info)
    {
        char file[128];
        static int i;
        snprintf (file, sizeof(file), "xvout_p%d_%03d.bmp", pPort->index, i++);
        secUtilDoDumpVBuf (pSec->dump_info, dst, file);
    }

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
    {
        DamageDamageRegion (pDamageDrawable, pPort->d.clip_boxes);
    }
    else if (pPort->layer)
    {
        SECVideoBuf *vbuf = secLayerGetBuffer (pPort->layer);
        Bool reset_layer = FALSE;
        xRectangle src_rect, dst_rect;

        if (vbuf)
            if (vbuf->width != pPort->out_width || vbuf->height != pPort->out_height)
                reset_layer = TRUE;

        secLayerGetRect (pPort->layer, &src_rect, &dst_rect);
        if (memcmp (&src_rect, &pPort->out_crop, sizeof(xRectangle)) ||
            dst_rect.x != pPort->d.dst.x ||
            dst_rect.y != pPort->d.dst.y ||
            dst_rect.width != pPort->out_crop.width ||
            dst_rect.height != pPort->out_crop.height)
            reset_layer = TRUE;

        if (reset_layer)
        {
            secLayerFreezeUpdate (pPort->layer, TRUE);

            src_rect = pPort->out_crop;
            dst_rect.x = pPort->d.dst.x;
            dst_rect.y = pPort->d.dst.y;
            dst_rect.width = pPort->out_crop.width;
            dst_rect.height = pPort->out_crop.height;

            secLayerSetRect (pPort->layer, &src_rect, &dst_rect);
            secLayerFreezeUpdate (pPort->layer, FALSE);
            secLayerSetBuffer (pPort->layer, dst);
        }
        else
            secLayerSetBuffer (pPort->layer, dst);

        if (!secLayerIsVisible (pPort->layer))
            secLayerShow (pPort->layer);
    }

    XDBG_DEBUG (MVDO, "++++++++++++++++++++++++.. \n");
}

static void
_secVideoTvoutCvtCallback (SECCvt *cvt,
                           SECVideoBuf *src,
                           SECVideoBuf *dst,
                           void *cvt_data,
                           Bool error)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)cvt_data;

    XDBG_RETURN_IF_FAIL (pPort != NULL);
    XDBG_RETURN_IF_FAIL (cvt != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (src));
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (dst));

    XDBG_DEBUG (MVDO, "######################## \n");
    XDBG_DEBUG (MVDO, "cvt(%p) src(%p) dst(%p)\n", cvt, src, dst);

    if (pPort->wait_vbuf != src)
        XDBG_WARNING (MVDO, "wait_vbuf(%p) != src(%p). \n",
                      pPort->wait_vbuf, src);

    pPort->wait_vbuf = NULL;

    XDBG_DEBUG (MVDO, "########################.. \n");
}

static void
_secVideoLayerNotifyFunc (SECLayer *layer, int type, void *type_data, void *data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr)data;
    SECVideoBuf *vbuf = (SECVideoBuf*)type_data;

    if (type != LAYER_VBLANK)
        return;

    XDBG_RETURN_IF_FAIL (pPort != NULL);
    XDBG_RETURN_IF_FAIL (VBUF_IS_VALID (vbuf));

    if (pPort->wait_vbuf != vbuf)
        XDBG_WARNING (MVDO, "wait_vbuf(%p) != vbuf(%p). \n",
                      pPort->wait_vbuf, vbuf);

    XDBG_DEBUG (MVBUF, "now_showing(%p). \n", vbuf);

    pPort->wait_vbuf = NULL;
}

static void
_secVideoEnsureConverter (SECPortPrivPtr pPort)
{
    if (pPort->cvt)
        return;

    pPort->cvt = secCvtCreate (pPort->pScrn, CVT_OP_M2M);
    XDBG_RETURN_IF_FAIL (pPort->cvt != NULL);

    secCvtAddCallback (pPort->cvt, _secVideoCvtCallback, pPort);
}

static void
_secVideoCloseConverter (SECPortPrivPtr pPort)
{
    if (pPort->cvt)
    {
        secCvtDestroy (pPort->cvt);
        pPort->cvt = NULL;
    }

    XDBG_TRACE (MVDO, "done. \n");
}

static void
_secVideoStreamOff (SECPortPrivPtr pPort)
{
    _secVideoCloseConverter (pPort);
    _secVideoCloseInBuffer (pPort);
    _secVideoCloseOutBuffer (pPort, TRUE);

    SECWb *wb = secWbGet ();
    if (wb)
    {
        if (pPort->need_start_wb)
        {
            secWbSetSecure (wb, FALSE);
            secWbStart (wb);
            pPort->need_start_wb = FALSE;
        }
        else
            secWbSetSecure (wb, FALSE);
    }

    if (pPort->d.clip_boxes)
    {
        RegionDestroy (pPort->d.clip_boxes);
        pPort->d.clip_boxes = NULL;
    }

    memset (&pPort->old_d, 0, sizeof (PutData));
    memset (&pPort->d, 0, sizeof (PutData));

    pPort->need_start_wb = FALSE;
    pPort->skip_tvout = FALSE;
    pPort->usr_output = OUTPUT_LCD|OUTPUT_EXT;
    pPort->outbuf_cvting = -1;
    pPort->drawing = 0;
    pPort->tv_prev_time = 0;
    pPort->secure = FALSE;
    pPort->csc_range = 0;
    pPort->inbuf_is_fb = FALSE;

    if (pPort->stream_cnt > 0)
    {
        pPort->stream_cnt = 0;
        XDBG_SECURE (MVDO, "pPort(%d) stream off. \n", pPort->index);

        if (pPort->preemption > -1)
            streaming_ports--;

        XDBG_WARNING_IF_FAIL (streaming_ports >= 0);
    }

    XDBG_TRACE (MVDO, "done. \n");
}

static Bool
_secVideoCalculateSize (SECPortPrivPtr pPort)
{
    SECCvtProp src_prop = {0,}, dst_prop = {0,};

    src_prop.id = pPort->d.id;
    src_prop.width = pPort->d.width;
    src_prop.height = pPort->d.height;
    src_prop.crop = pPort->d.src;

     dst_prop.id = FOURCC_RGB32;
    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
    {
        dst_prop.width = pPort->d.pDraw->width;
        dst_prop.height = pPort->d.pDraw->height;
        dst_prop.crop = pPort->d.dst;
        dst_prop.crop.x -= pPort->d.pDraw->x;
        dst_prop.crop.y -= pPort->d.pDraw->y;
    }
    else
    {
        dst_prop.width = pPort->d.dst.width;
        dst_prop.height = pPort->d.dst.height;
        dst_prop.crop = pPort->d.dst;
        dst_prop.crop.x = 0;
        dst_prop.crop.y = 0;
    }

    XDBG_DEBUG (MVDO, "(%dx%d : %d,%d %dx%d) => (%dx%d : %d,%d %dx%d)\n",
                src_prop.width, src_prop.height,
                src_prop.crop.x, src_prop.crop.y, src_prop.crop.width, src_prop.crop.height,
                dst_prop.width, dst_prop.height,
                dst_prop.crop.x, dst_prop.crop.y, dst_prop.crop.width, dst_prop.crop.height);

    if (!secCvtEnsureSize (&src_prop, &dst_prop))
        return FALSE;

    XDBG_DEBUG (MVDO, "(%dx%d : %d,%d %dx%d) => (%dx%d : %d,%d %dx%d)\n",
                src_prop.width, src_prop.height,
                src_prop.crop.x, src_prop.crop.y, src_prop.crop.width, src_prop.crop.height,
                dst_prop.width, dst_prop.height,
                dst_prop.crop.x, dst_prop.crop.y, dst_prop.crop.width, dst_prop.crop.height);

    XDBG_RETURN_VAL_IF_FAIL (src_prop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src_prop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src_prop.crop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (src_prop.crop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst_prop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst_prop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst_prop.crop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (dst_prop.crop.height > 0, FALSE);

    pPort->in_width = src_prop.width;
    pPort->in_height = src_prop.height;
    pPort->in_crop = src_prop.crop;

    pPort->out_width = dst_prop.width;
    pPort->out_height = dst_prop.height;
    pPort->out_crop = dst_prop.crop;

    return TRUE;
}

static void
_secVideoPunchDrawable (SECPortPrivPtr pPort)
{
    PixmapPtr pPixmap = _getPixmap (pPort->d.pDraw);
    SECPtr pSec = SECPTR (pPort->pScrn);

    if (pPort->drawing != ON_FB || !pSec->pVideoPriv->video_punch)
        return;

    if (!pPort->punched)
    {
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        if (pPixmap->devPrivate.ptr)
            memset (pPixmap->devPrivate.ptr, 0,
                    pPixmap->drawable.width * pPixmap->drawable.height * 4);
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_TRACE (MVDO, "Punched (%dx%d) %p. \n",
                    pPixmap->drawable.width, pPixmap->drawable.height,
                    pPixmap->devPrivate.ptr);
        pPort->punched = TRUE;
        DamageDamageRegion (pPort->d.pDraw, pPort->d.clip_boxes);
    }
}

static Bool
_secVideoSupportID (int id)
{
    int i;

    for (i = 0; i < NUM_IMAGES; i++)
        if (images[i].id == id)
            if (secCvtSupportFormat (CVT_OP_M2M, id))
                return TRUE;

    return FALSE;
}

static Bool
_secVideoInBranch (WindowPtr p, WindowPtr w)
{
    for (; w; w = w->parent)
        if (w == p)
            return TRUE;

    return FALSE;
}

/* Return the child of 'p' which includes 'w'. */
static WindowPtr
_secVideoGetChild (WindowPtr p, WindowPtr w)
{
    WindowPtr c;

    for (c = w, w = w->parent; w; c = w, w = w->parent)
        if (w == p)
            return c;

    return NULL;
}

/* ancestor : Return the parent of 'a' and 'b'.
 * ancestor_a : Return the child of 'ancestor' which includes 'a'.
 * ancestor_b : Return the child of 'ancestor' which includes 'b'.
 */
static Bool
_secVideoGetAncestors (WindowPtr a, WindowPtr b,
                       WindowPtr *ancestor,
                       WindowPtr *ancestor_a,
                       WindowPtr *ancestor_b)
{
    WindowPtr child_a, child_b;

    if (!ancestor || !ancestor_a || !ancestor_b)
        return FALSE;

    for (child_b = b, b = b->parent; b; child_b = b, b = b->parent)
    {
        child_a = _secVideoGetChild (b, a);
        if (child_a)
        {
            *ancestor   = b;
            *ancestor_a = child_a;
            *ancestor_b = child_b;
            return TRUE;
        }
    }

    return FALSE;
}

static int
_secVideoCompareWindow (WindowPtr pWin1, WindowPtr pWin2)
{
    WindowPtr a, a1, a2, c;

    if (!pWin1 || !pWin2)
        return -2;

    if (pWin1 == pWin2)
        return 0;

    if (_secVideoGetChild (pWin1, pWin2))
        return -1;

    if (_secVideoGetChild (pWin2, pWin1))
        return 1;

    if (!_secVideoGetAncestors (pWin1, pWin2, &a, &a1, &a2))
        return -3;

    for (c = a->firstChild; c; c = c->nextSib)
    {
        if (c == a1)
            return 1;
        else if (c == a2)
            return -1;
    }

    return -4;
}

static void
_secVideoArrangeLayerPos (SECPortPrivPtr pPort, Bool by_notify)
{
    SECPortPrivPtr pCur = NULL, pNext = NULL;
    SECPortPrivPtr pAnother = NULL;
    int i = 0;

    xorg_list_for_each_entry_safe (pCur, pNext, &layer_owners, link)
    {
        if (pCur == pPort)
            continue;

        i++;

        if (!pAnother)
            pAnother = pCur;
        else
            XDBG_WARNING (MVDO, "There are 3 more V4L2 ports. (%d) \n", i);
    }

    if (!pAnother)
    {
        SECLayerPos lpos = secLayerGetPos (pPort->layer);

        if (lpos == LAYER_NONE)
            secLayerSetPos (pPort->layer, LAYER_LOWER2);
    }
    else
    {
        SECLayerPos lpos1 = LAYER_NONE;
        SECLayerPos lpos2 = LAYER_NONE;

        if (pAnother->layer)
            lpos1 = secLayerGetPos (pAnother->layer);
        if (pPort->layer)
            lpos2 = secLayerGetPos (pPort->layer);

        if (lpos2 == LAYER_NONE)
        {
            int comp = _secVideoCompareWindow ((WindowPtr)pAnother->d.pDraw,
                                               (WindowPtr)pPort->d.pDraw);

            XDBG_TRACE (MVDO, "0x%08x : 0x%08x => %d \n",
                        _XID(pAnother->d.pDraw), _XID(pPort->d.pDraw), comp);

            if (comp == 1)
            {
                if (lpos1 != LAYER_LOWER1)
                    secLayerSetPos (pAnother->layer, LAYER_LOWER1);
                secLayerSetPos (pPort->layer, LAYER_LOWER2);
            }
            else if (comp == -1)
            {
                if (lpos1 != LAYER_LOWER2)
                    secLayerSetPos (pAnother->layer, LAYER_LOWER2);
                secLayerSetPos (pPort->layer, LAYER_LOWER1);
            }
            else
            {
                if (lpos1 == LAYER_LOWER1)
                    secLayerSetPos (pPort->layer, LAYER_LOWER2);
                else
                    secLayerSetPos (pPort->layer, LAYER_LOWER1);
            }
        }
        else
        {
            if (!by_notify)
                return;

            int comp = _secVideoCompareWindow ((WindowPtr)pAnother->d.pDraw,
                                               (WindowPtr)pPort->d.pDraw);

            XDBG_TRACE (MVDO, "0x%08x : 0x%08x => %d \n",
                        _XID(pAnother->d.pDraw), _XID(pPort->d.pDraw), comp);

            if ((comp == 1 && lpos1 != LAYER_LOWER1) ||
                (comp == -1 && lpos2 != LAYER_LOWER1))
                secLayerSwapPos (pAnother->layer, pPort->layer);
        }
    }
}

static void
_secVideoStopTvout (ScrnInfoPtr pScrn)
{
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pSec->pVideoPriv->pAdaptor[0];
    int i;

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        SECPortPrivPtr pPort = (SECPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->grab_tvout)
        {
            _secVideoUngrabTvout (pPort);
            return;
        }
    }
}

/* TRUE  : current frame will be shown on TV. free after vblank.
 * FALSE : current frame won't be shown on TV.
 */
static Bool
_secVideoPutImageTvout (SECPortPrivPtr pPort, int output, SECVideoBuf *inbuf)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    SECModePtr pSecMode = (SECModePtr) SECPTR (pScrn)->pSecMode;
    xRectangle tv_rect = {0,};
    Bool first_put = FALSE;

    if (!(output & OUTPUT_EXT))
        return FALSE;

    if (pPort->skip_tvout)
        return FALSE;

    if (!_secVideoGrabTvout(pPort))
        goto fail_to_put_tvout;

    if (!pPort->tv)
    {
        SECCvt *tv_cvt;
        SECWb  *wb;

        if (!secUtilEnsureExternalCrtc (pScrn))
        {
            XDBG_ERROR (MVDO, "failed : pPort(%d) connect external crtc\n", pPort->index);
            goto fail_to_put_tvout;
        }

        pPort->tv = secVideoTvConnect (pScrn, pPort->d.id, LAYER_LOWER1);
        XDBG_GOTO_IF_FAIL (pPort->tv != NULL, fail_to_put_tvout);

        wb = secWbGet ();
        if (wb)
        {
            pPort->need_start_wb = TRUE;

            /* in case of VIRTUAL, wb's buffer is used by tvout. */
            if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
                secWbStop (wb, FALSE);
            else
                secWbStop (wb, TRUE);
        }

        if (secWbIsRunning ())
        {
            XDBG_ERROR (MVDO, "failed: wb still running\n");
            goto fail_to_put_tvout;
        }

        tv_cvt = secVideoTvGetConverter (pPort->tv);
        if (tv_cvt)
        {
            /* HDMI    : SN12
             * VIRTUAL : SN12 or RGB32
             */
            if (pSecMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
            {
                if (pSecMode->set_mode == DISPLAY_SET_MODE_CLONE)
                {
                    SECVideoBuf **vbufs = NULL;
                    int bufnum = 0;

                    secVideoTvSetConvertFormat (pPort->tv, FOURCC_SN12);

                    /* In case of virtual, we draw video on full-size buffer
                     * for virtual-adaptor
                     */
                    secVideoTvSetSize (pPort->tv,
                                       pSecMode->ext_connector_mode.hdisplay,
                                       pSecMode->ext_connector_mode.vdisplay);

                    secVirtualVideoGetBuffers (pPort->pScrn, FOURCC_SN12,
                                               pSecMode->ext_connector_mode.hdisplay,
                                               pSecMode->ext_connector_mode.vdisplay,
                                               &vbufs, &bufnum);

                    XDBG_GOTO_IF_FAIL (vbufs != NULL, fail_to_put_tvout);
                    XDBG_GOTO_IF_FAIL (bufnum > 0, fail_to_put_tvout);

                    secVideoTvSetBuffer (pPort->tv, vbufs, bufnum);
                }
                else /* desktop */
                    secVideoTvSetConvertFormat (pPort->tv, FOURCC_RGB32);
            }
            else
                secVideoTvSetConvertFormat (pPort->tv, FOURCC_SN12);

            secCvtAddCallback (tv_cvt, _secVideoTvoutCvtCallback, pPort);
        }
        else
        {
            SECLayer *layer = secVideoTvGetLayer (pPort->tv);
            XDBG_GOTO_IF_FAIL (layer != NULL, fail_to_put_tvout);

            secLayerEnableVBlank (layer, TRUE);
            secLayerAddNotifyFunc (layer, _secVideoLayerNotifyFunc, pPort);
        }

        first_put = TRUE;
    }

    SECPtr pSec = SECPTR (pPort->pScrn);
    if (pPort->wait_vbuf)
    {
        if (pSec->pVideoPriv->video_fps)
        {
            CARD32 cur, sub;
            cur = GetTimeInMillis ();
            sub = cur - pPort->tv_prev_time;
            pPort->tv_prev_time = cur;

            XDBG_DEBUG (MVDO, "tvout skip : sub(%ld) vbuf(%ld:%d,%d,%d) \n",
                        sub, inbuf->stamp,
                        inbuf->keys[0], inbuf->keys[1], inbuf->keys[2]);
        }

        return FALSE;
    }
    else if (pSec->pVideoPriv->video_fps)
        pPort->tv_prev_time = GetTimeInMillis ();

    if (!(output & OUTPUT_FULL))
    {
        tv_rect.x = pPort->d.dst.x
                    - pSecMode->main_lcd_mode.hdisplay;
        tv_rect.y = pPort->d.dst.y;
        tv_rect.width = pPort->d.dst.width;
        tv_rect.height = pPort->d.dst.height;
    }
    else
    {
        secUtilAlignRect (pPort->d.src.width, pPort->d.src.height,
                          pSecMode->ext_connector_mode.hdisplay,
                          pSecMode->ext_connector_mode.vdisplay,
                          &tv_rect, TRUE);
    }

    /* if secVideoTvPutImage returns FALSE, it means this frame won't show on TV. */
    if (!secVideoTvPutImage (pPort->tv, inbuf, &tv_rect, pPort->csc_range))
        return FALSE;

    if (first_put && !(output & OUTPUT_LCD))
        _secVideoSetOutputExternalProperty (pPort->d.pDraw, TRUE);

    pPort->wait_vbuf = inbuf;

    return TRUE;

fail_to_put_tvout:
    _secVideoUngrabTvout (pPort);

    pPort->skip_tvout = TRUE;

    XDBG_TRACE (MVDO, "pPort(%d) skip tvout \n", pPort->index);

    return FALSE;
}

static Bool
_secVideoPutImageInbuf (SECPortPrivPtr pPort, SECVideoBuf *inbuf)
{
    if (!pPort->layer)
    {
        pPort->layer = _secVideoCreateLayer (pPort);
        XDBG_RETURN_VAL_IF_FAIL (pPort->layer != NULL, FALSE);

        _secVideoArrangeLayerPos (pPort, FALSE);
    }

    secLayerSetBuffer (pPort->layer, inbuf);

    if (!secLayerIsVisible (pPort->layer))
        secLayerShow (pPort->layer);

    return TRUE;
}

static Bool
_secVideoPutImageInternal (SECPortPrivPtr pPort, SECVideoBuf *inbuf)
{
    SECPtr pSec = (SECPtr) pPort->pScrn->driverPrivate;
    SECCvtProp src_prop = {0,}, dst_prop = {0,};
    SECVideoBuf *outbuf = NULL;

    outbuf = _secVideoGetOutbuf (pPort);
    if (!outbuf)
        return FALSE;

    /* cacheflush here becasue dst buffer can be created in _secVideoGetOutbuf() */
    if (pPort->stream_cnt == 1)
        if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
            secUtilCacheFlush (pPort->pScrn);

    XDBG_DEBUG (MVDO, "'%c%c%c%c' preem(%d) rot(%d) \n",
                FOURCC_STR (pPort->d.id),
                pPort->preemption, pPort->hw_rotate);

    if (pPort->layer)
        _secVideoArrangeLayerPos (pPort, FALSE);

    _secVideoEnsureConverter (pPort);
    XDBG_GOTO_IF_FAIL (pPort->cvt != NULL, fail_to_put);

    src_prop.id = pPort->d.id;
    src_prop.width = pPort->in_width;
    src_prop.height = pPort->in_height;
    src_prop.crop = pPort->in_crop;

    dst_prop.id = FOURCC_RGB32;
    dst_prop.width = pPort->out_width;
    dst_prop.height = pPort->out_height;
    dst_prop.crop = pPort->out_crop;

    dst_prop.degree = pPort->hw_rotate;
    dst_prop.hflip = pPort->hflip;
    dst_prop.vflip = pPort->vflip;
    dst_prop.secure = pPort->secure;
    dst_prop.csc_range = pPort->csc_range;

    if (!secCvtEnsureSize (&src_prop, &dst_prop))
        goto fail_to_put;

    if (!secCvtSetProperpty (pPort->cvt, &src_prop, &dst_prop))
        goto fail_to_put;

    if (!secCvtConvert (pPort->cvt, inbuf, outbuf))
        goto fail_to_put;

    if (pSec->pVideoPriv->video_fps)
        _countFps (pPort);

    secUtilVideoBufferUnref (outbuf);

    return TRUE;

fail_to_put:
    if (outbuf)
        secUtilVideoBufferUnref (outbuf);

    _secVideoCloseConverter (pPort);
    _secVideoCloseOutBuffer (pPort, TRUE);

    return FALSE;
}

static Bool
_secVideoSetHWPortsProperty (ScreenPtr pScreen, int nums)
{
    WindowPtr pWin = pScreen->root;
    Atom atom_hw_ports;

    /* With "X_HW_PORTS", an application can know
     * how many fimc devices XV uses.
     */
    if (!pWin || !serverClient)
        return FALSE;

    atom_hw_ports = MakeAtom ("XV_HW_PORTS", strlen ("XV_HW_PORTS"), TRUE);

    dixChangeWindowProperty (serverClient,
                             pWin, atom_hw_ports, XA_CARDINAL, 32,
                             PropModeReplace, 1, (unsigned int*)&nums, FALSE);

    return TRUE;
}

static Bool
_secVideoSetOutputExternalProperty (DrawablePtr pDraw, Bool video_only)
{
    WindowPtr pWin;
    Atom atom_external;

    XDBG_RETURN_VAL_IF_FAIL (pDraw != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL (pDraw->type == DRAWABLE_WINDOW, FALSE);

    pWin = (WindowPtr)pDraw;

    atom_external = MakeAtom ("XV_OUTPUT_EXTERNAL", strlen ("XV_OUTPUT_EXTERNAL"), TRUE);

    dixChangeWindowProperty (clients[CLIENT_ID(pDraw->id)],
                             pWin, atom_external, XA_CARDINAL, 32,
                             PropModeReplace, 1, (unsigned int*)&video_only, TRUE);

    XDBG_TRACE (MVDO, "pDraw(0x%08x) video-only(%s)\n",
                pDraw->id, (video_only)?"ON":"OFF");

    return TRUE;
}

static void
_secVideoRestackWindow (WindowPtr pWin, WindowPtr pOldNextSib)
{
    ScreenPtr pScreen = ((DrawablePtr)pWin)->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    SECVideoPrivPtr pVideo = pSec->pVideoPriv;

    if (pVideo->RestackWindow)
    {
        pScreen->RestackWindow = pVideo->RestackWindow;

        if (pScreen->RestackWindow)
            (*pScreen->RestackWindow)(pWin, pOldNextSib);

        pVideo->RestackWindow = pScreen->RestackWindow;
        pScreen->RestackWindow = _secVideoRestackWindow;
    }

    if (!xorg_list_is_empty (&layer_owners))
    {
        SECPortPrivPtr pCur = NULL, pNext = NULL;
        xorg_list_for_each_entry_safe (pCur, pNext, &layer_owners, link)
        {
            if (_secVideoInBranch (pWin, (WindowPtr)pCur->d.pDraw))
            {
                XDBG_TRACE (MVDO, "Do re-arrange. 0x%08x(0x%08x) \n",
                            _XID(pWin), _XID(pCur->d.pDraw));
                _secVideoArrangeLayerPos (pCur, TRUE);
                break;
            }
        }
    }
}

static void
_secVideoBlockHandler (pointer data, OSTimePtr pTimeout, pointer pRead)
{
    ScreenPtr pScreen = ((ScrnInfoPtr)data)->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    SECVideoPrivPtr pVideo = pSec->pVideoPriv;

    pVideo->RestackWindow = pScreen->RestackWindow;
    pScreen->RestackWindow = _secVideoRestackWindow;

    if(registered_handler && _secVideoSetHWPortsProperty (pScreen, NUM_HW_LAYER))
    {
        RemoveBlockAndWakeupHandlers(_secVideoBlockHandler,
                                     (WakeupHandlerProcPtr)NoopDDA, data);
        registered_handler = FALSE;
    }
}

static Bool
_secVideoAddDrawableEvent (SECPortPrivPtr pPort)
{
    SECVideoResource *resource;
    void *ptr=NULL;
    int ret;

    ret = dixLookupResourceByType (&ptr, pPort->d.pDraw->id,
                             event_drawable_type, NULL, DixWriteAccess);
    if (ret == Success)
    {
        return TRUE;
    }

    resource = malloc (sizeof (SECVideoResource));
    if (resource == NULL)
        return FALSE;

    if (!AddResource (pPort->d.pDraw->id, event_drawable_type, resource))
    {
        free (resource);
        return FALSE;
    }

    XDBG_TRACE (MVDO, "id(0x%08lx). \n", pPort->d.pDraw->id);

    resource->id = pPort->d.pDraw->id;
    resource->type = event_drawable_type;
    resource->pPort = pPort;
    resource->pScrn = pPort->pScrn;

    return TRUE;
}

static int
_secVideoRegisterEventDrawableGone (void *data, XID id)
{
    SECVideoResource *resource = (SECVideoResource*)data;

    XDBG_TRACE (MVDO, "id(0x%08lx). \n", id);

    if (!resource)
        return Success;

    if (!resource->pPort || !resource->pScrn)
        return Success;

    SECVideoStop (resource->pScrn, (pointer)resource->pPort, 1);

    free(resource);

    return Success;
}

static Bool
_secVideoRegisterEventResourceTypes (void)
{
    event_drawable_type = CreateNewResourceType (_secVideoRegisterEventDrawableGone, "Sec Video Drawable");

    if (!event_drawable_type)
        return FALSE;

    return TRUE;
}

int
secVideoQueryImageAttrs (ScrnInfoPtr  pScrn,
                         int          id,
                         int         *w,
                         int         *h,
                         int         *pitches,
                         int         *offsets,
                         int         *lengths)
{
    int size = 0, tmp = 0;

    *w = (*w + 1) & ~1;
    if (offsets)
        offsets[0] = 0;

    switch (id)
    {
    /* RGB565 */
    case FOURCC_SR16:
    case FOURCC_RGB565:
        size += (*w << 1);
        if (pitches)
            pitches[0] = size;
        size *= *h;
        if (lengths)
            lengths[0] = size;
        break;
    /* RGB32 */
    case FOURCC_SR32:
    case FOURCC_RGB32:
        size += (*w << 2);
        if (pitches)
            pitches[0] = size;
        size *= *h;
        if (lengths)
            lengths[0] = size;
        break;
    /* YUV420, 3 planar */
    case FOURCC_I420:
    case FOURCC_S420:
    case FOURCC_YV12:
        *h = (*h + 1) & ~1;
        size = (*w + 3) & ~3;
        if (pitches)
            pitches[0] = size;

        size *= *h;
        if (offsets)
            offsets[1] = size;
        if (lengths)
            lengths[0] = size;

        tmp = ((*w >> 1) + 3) & ~3;
        if (pitches)
            pitches[1] = pitches[2] = tmp;

        tmp *= (*h >> 1);
        size += tmp;
        if (offsets)
            offsets[2] = size;
        if (lengths)
            lengths[1] = tmp;

        size += tmp;
        if (lengths)
            lengths[2] = tmp;

        break;
    /* YUV422, packed */
    case FOURCC_UYVY:
    case FOURCC_SYVY:
    case FOURCC_ITLV:
    case FOURCC_SUYV:
    case FOURCC_YUY2:
        size = *w << 1;
        if (pitches)
            pitches[0] = size;

        size *= *h;
        if (lengths)
            lengths[0] = size;
        break;

    /* YUV420, 2 planar */
    case FOURCC_SN12:
    case FOURCC_NV12:
    case FOURCC_SN21:
    case FOURCC_NV21:
        if (pitches)
            pitches[0] = *w;

        size = (*w) * (*h);
        if (offsets)
            offsets[1] = size;
        if (lengths)
            lengths[0] = size;

        if (pitches)
            pitches[1] = *w >> 1;

        tmp = (*w) * (*h >> 1);
        size += tmp;
        if (lengths)
            lengths[1] = tmp;
        break;

    /* YUV420, 2 planar, tiled */
    case FOURCC_ST12:
        if (pitches)
            pitches[0] = *w;

        size = ALIGN_TO_8KB(ALIGN_TO_128B(*w) * ALIGN_TO_32B(*h));
        if (offsets)
            offsets[1] = size;
        if (lengths)
            lengths[0] = size;

        if (pitches)
            pitches[1] = *w >> 1;

        tmp = ALIGN_TO_8KB(ALIGN_TO_128B(*w) * ALIGN_TO_32B(*h >> 1));
        size += tmp;
        if (lengths)
            lengths[1] = tmp;
        break;
    default:
        return 0;
    }

    return size;
}

static int
SECVideoGetPortAttribute (ScrnInfoPtr pScrn,
                          Atom        attribute,
                          INT32      *value,
                          pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_ROTATION))
    {
        *value = pPort->rotate;
        return Success;
    }
    else if (attribute == _portAtom (PAA_HFLIP))
    {
        *value = pPort->hflip;
        return Success;
    }
    else if (attribute == _portAtom (PAA_VFLIP))
    {
        *value = pPort->vflip;
        return Success;
    }
    else if (attribute == _portAtom (PAA_PREEMPTION))
    {
        *value = pPort->preemption;
        return Success;
    }
    else if (attribute == _portAtom (PAA_OUTPUT))
    {
        *value = pPort->usr_output;
        return Success;
    }
    else if (attribute == _portAtom (PAA_SECURE))
    {
        *value = pPort->secure;
        return Success;
    }
    else if (attribute == _portAtom (PAA_CSC_RANGE))
    {
        *value = pPort->csc_range;
        return Success;
    }

    return BadMatch;
}

static int
SECVideoSetPortAttribute (ScrnInfoPtr pScrn,
                          Atom        attribute,
                          INT32       value,
                          pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_ROTATION))
    {
        pPort->rotate = value;
        XDBG_DEBUG (MVDO, "rotate(%d) \n", value);
        return Success;
    }
    else if (attribute == _portAtom (PAA_HFLIP))
    {
        pPort->hflip = value;
        XDBG_DEBUG (MVDO, "hflip(%d) \n", value);
        return Success;
    }
    else if (attribute == _portAtom (PAA_VFLIP))
    {
        pPort->vflip = value;
        XDBG_DEBUG (MVDO, "vflip(%d) \n", value);
        return Success;
    }
    else if (attribute == _portAtom (PAA_PREEMPTION))
    {
        pPort->preemption = value;
        XDBG_DEBUG (MVDO, "preemption(%d) \n", value);
        return Success;
    }
    else if (attribute == _portAtom (PAA_OUTPUT))
    {
        if (value == OUTPUT_MODE_TVOUT)
            pPort->usr_output = OUTPUT_LCD|OUTPUT_EXT|OUTPUT_FULL;
        else if (value == OUTPUT_MODE_EXT_ONLY)
            pPort->usr_output = OUTPUT_EXT|OUTPUT_FULL;
        else
            pPort->usr_output = OUTPUT_LCD|OUTPUT_EXT;

        XDBG_DEBUG (MVDO, "output (%d) \n", value);

        return Success;
    }
    else if (attribute == _portAtom (PAA_SECURE))
    {
        pPort->secure = value;
        XDBG_DEBUG (MVDO, "secure(%d) \n", value);
        return Success;
    }
    else if (attribute == _portAtom (PAA_CSC_RANGE))
    {
        pPort->csc_range = value;
        XDBG_DEBUG (MVDO, "csc_range(%d) \n", value);
        return Success;
    }

    return Success;
}

static void
SECVideoQueryBestSize (ScrnInfoPtr pScrn,
                       Bool motion,
                       short vid_w, short vid_h,
                       short dst_w, short dst_h,
                       uint *p_w, uint *p_h,
                       pointer data)
{
    SECCvtProp prop = {0,};

    if (!p_w && !p_h)
        return;

    prop.width = dst_w;
    prop.height = dst_h;
    prop.crop.width = dst_w;
    prop.crop.height = dst_h;

    if (secCvtEnsureSize (NULL, &prop))
    {
        if (p_w)
            *p_w = prop.width;
        if (p_h)
            *p_h = prop.height;
    }
    else
    {
        if (p_w)
            *p_w = dst_w;
        if (p_h)
            *p_h = dst_h;
    }
}

/**
 * Give image size and pitches.
 */
static int
SECVideoQueryImageAttributes (ScrnInfoPtr    pScrn,
                              int            id,
                              unsigned short *w,
                              unsigned short *h,
                              int            *pitches,
                              int            *offsets)
{
    int width, height, size;

    if (!w || !h)
        return 0;

    width = (int)*w;
    height = (int)*h;

    size = secVideoQueryImageAttrs (pScrn, id, &width, &height, pitches, offsets, NULL);

    *w = (unsigned short)width;
    *h = (unsigned short)height;

    return size;
}

/* coordinates : HW, SCREEN, PORT
 * BadRequest : when video can't be shown or drawn.
 * Success    : A damage event(pixmap) and inbuf should be return.
 *              If can't return a damage event and inbuf, should be return
 *              BadRequest.
 */
static int
SECVideoPutImage (ScrnInfoPtr pScrn,
                  short src_x, short src_y, short dst_x, short dst_y,
                  short src_w, short src_h, short dst_w, short dst_h,
                  int id, uchar *buf, short width, short height,
                  Bool sync, RegionPtr clip_boxes, pointer data,
                  DrawablePtr pDraw)
{
    SECPtr pSec = SECPTR (pScrn);
    SECModePtr pSecMode = (SECModePtr)SECPTR (pScrn)->pSecMode;
    SECVideoPrivPtr pVideo = SECPTR (pScrn)->pVideoPriv;
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;
    int output, ret;
    Bool tvout = FALSE, lcdout = FALSE;
    SECVideoBuf *inbuf = NULL;
    int old_drawing;

    if (!_secVideoSupportID (id))
    {
        XDBG_ERROR (MVDO, "'%c%c%c%c' not supported.\n", FOURCC_STR (id));
        return BadRequest;
    }

    XDBG_TRACE (MVDO, "======================================= \n");

    pPort->pScrn = pScrn;
    pPort->d.id = id;
    pPort->d.buf = buf;

    if (pSec->xvperf_mode & XBERC_XVPERF_MODE_IA)
    {
        unsigned int keys[PLANAR_CNT] = {0,};
        CARD32 cur, sub;
        char temp[64];
        cur = GetTimeInMillis ();
        sub = cur - pPort->prev_time;
        pPort->prev_time = cur;
        temp[0] = '\0';
        if (IS_ZEROCOPY (id))
        {
            _secVideoGetKeys (pPort, keys, NULL);
            snprintf (temp, sizeof(temp), "%d,%d,%d", keys[0], keys[1], keys[2]);
        }
        ErrorF ("pPort(%p) put interval(%s) : %6ld ms\n", pPort, temp, sub);
    }

    if (IS_ZEROCOPY (pPort->d.id))
    {
        unsigned int keys[PLANAR_CNT] = {0,};
        int i;

        if (_secVideoGetKeys (pPort, keys, NULL))
            return BadRequest;

        for (i = 0; i < INBUF_NUM; i++)
            if (pPort->inbuf[i] && pPort->inbuf[i]->keys[0] == keys[0])
            {
                XDBG_WARNING (MVDO, "got flink_id(%d) twice!\n", keys[0]);
                _secVideoSendReturnBufferMessage (pPort, NULL, keys);
                return Success;
            }
    }

    pPort->d.width = width;
    pPort->d.height = height;
    pPort->d.src.x = src_x;
    pPort->d.src.y = src_y;
    pPort->d.src.width = src_w;
    pPort->d.src.height = src_h;
    pPort->d.dst.x = dst_x;    /* included pDraw'x */
    pPort->d.dst.y = dst_y;    /* included pDraw'y */
    pPort->d.dst.width = dst_w;
    pPort->d.dst.height = dst_h;
    pPort->d.sync = FALSE;
    if (sync)
        XDBG_WARNING (MVDO, "not support sync.\n");
    pPort->d.data = data;
    pPort->d.pDraw = pDraw;
    if (clip_boxes)
    {
        if (!pPort->d.clip_boxes)
            pPort->d.clip_boxes = RegionCreate(NullBox, 0);
        RegionCopy (pPort->d.clip_boxes, clip_boxes);
    }

    old_drawing = pPort->drawing;
    pPort->drawing = _secVideodrawingOn (pPort);
    if (old_drawing != pPort->drawing)
    {
        _secVideoCloseConverter (pPort);
        _secVideoCloseOutBuffer (pPort, TRUE);
    }

    _secVideoGetRotation (pPort, &pPort->hw_rotate);

    if (pPort->drawing == ON_FB && pVideo->screen_rotate_degree > 0)
        secUtilRotateRect (pSecMode->main_lcd_mode.hdisplay,
                           pSecMode->main_lcd_mode.vdisplay,
                           &pPort->d.dst,
                           pVideo->screen_rotate_degree);

    if (pPort->secure)
        if (pPort->drawing != ON_FB)
        {
            XDBG_ERROR (MVDO, "secure video should drawn on FB.\n");
            return BadRequest;
        }

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        if (!_secVideoAddDrawableEvent (pPort))
            return BadRequest;

    if (pPort->stream_cnt == 0)
    {
        pPort->stream_cnt++;

        if (pPort->preemption > -1)
            streaming_ports++;

        XDBG_SECURE (MVDO, "pPort(%d) streams(%d) rotate(%d) flip(%d,%d) secure(%d) range(%d) usr_output(%x) on(%s)\n",
                     pPort->index, streaming_ports,
                     pPort->rotate, pPort->hflip, pPort->vflip, pPort->secure, pPort->csc_range,
                     pPort->usr_output, drawing_type[pPort->drawing]);
        XDBG_SECURE (MVDO, "id(%c%c%c%c) sz(%dx%d) src(%d,%d %dx%d) dst(%d,%d %dx%d)\n",
                     FOURCC_STR (id), width, height,
                     src_x, src_y, src_w, src_h, dst_x, dst_y, dst_w, dst_h);

        if (streaming_ports > 1)
            _secVideoStopTvout (pPort->pScrn);
    }
    else if (pPort->stream_cnt == 1)
        pPort->stream_cnt++;

    if (pPort->cvt)
    {
        SECCvtProp dst_prop;

        secCvtGetProperpty (pPort->cvt, NULL, &dst_prop);

        if (pPort->d.id != pPort->old_d.id ||
            pPort->d.width != pPort->old_d.width ||
            pPort->d.height != pPort->old_d.height ||
            memcmp (&pPort->d.src, &pPort->old_d.src, sizeof (xRectangle)) ||
            dst_prop.degree != pPort->hw_rotate ||
            dst_prop.hflip != pPort->hflip ||
            dst_prop.vflip != pPort->vflip ||
            dst_prop.secure != pPort->secure ||
            dst_prop.csc_range != pPort->csc_range)
        {
            XDBG_DEBUG (MVDO, "pPort(%d) streams(%d) rotate(%d) flip(%d,%d) secure(%d) range(%d) usr_output(%x) on(%s)\n",
                        pPort->index, streaming_ports,
                        pPort->rotate, pPort->hflip, pPort->vflip, pPort->secure, pPort->csc_range,
                        pPort->usr_output, drawing_type[pPort->drawing]);
            XDBG_DEBUG (MVDO, "pPort(%d) old_src(%dx%d %d,%d %dx%d) : new_src(%dx%d %d,%d %dx%d)\n",
                        pPort->index, pPort->old_d.width, pPort->old_d.height,
                        pPort->old_d.src.x, pPort->old_d.src.y,
                        pPort->old_d.src.width, pPort->old_d.src.height,
                        pPort->d.width, pPort->d.height,
                        pPort->d.src.x, pPort->d.src.y,
                        pPort->d.src.width, pPort->d.src.height);
            _secVideoCloseConverter (pPort);
            _secVideoCloseInBuffer (pPort);
            pPort->inbuf_is_fb = FALSE;
        }
    }

    if (memcmp (&pPort->d.dst, &pPort->old_d.dst, sizeof (xRectangle)))
    {
        XDBG_DEBUG (MVDO, "pPort(%d) old_dst(%d,%d %dx%d) : new_dst(%dx%d %dx%d)\n",
                    pPort->index,
                    pPort->old_d.dst.x, pPort->old_d.dst.y,
                    pPort->old_d.dst.width, pPort->old_d.dst.height,
                    pPort->d.dst.x, pPort->d.dst.y,
                    pPort->d.dst.width, pPort->d.dst.height);
        _secVideoCloseConverter (pPort);
        _secVideoCloseOutBuffer (pPort, FALSE);
        pPort->inbuf_is_fb = FALSE;
    }

    if (!_secVideoCalculateSize (pPort))
        return BadRequest;

    output = _secVideoGetTvoutMode (pPort);
    if (!(output & OUTPUT_LCD) && pPort->old_output & OUTPUT_LCD)
    {
        /* If the video of LCD becomes off, we also turn off LCD layer. */
        if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        {
            PixmapPtr pPixmap = _getPixmap (pPort->d.pDraw);
            SECPixmapPriv *privPixmap = exaGetPixmapDriverPrivate (pPixmap);

            secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
            if (pPixmap->devPrivate.ptr && privPixmap->size > 0)
                memset (pPixmap->devPrivate.ptr, 0, privPixmap->size);
            secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);

            DamageDamageRegion (pPort->d.pDraw, pPort->d.clip_boxes);
        }
        else
        {
            _secVideoCloseConverter (pPort);
            _secVideoCloseOutBuffer (pPort, TRUE);
        }
    }

    if (pPort->d.id == FOURCC_SR32 &&
        pPort->in_crop.width == pPort->out_crop.width &&
        pPort->in_crop.height == pPort->out_crop.height &&
        pPort->hw_rotate == 0)
        pPort->inbuf_is_fb = TRUE;
    else
        pPort->inbuf_is_fb = FALSE;

    inbuf = _secVideoGetInbuf (pPort);
    if (!inbuf)
        return BadRequest;

    /* punch here not only LCD but also HDMI. */
    if (pPort->drawing == ON_FB)
        _secVideoPunchDrawable (pPort);

    /* HDMI */
    if (output & OUTPUT_EXT)
        tvout = _secVideoPutImageTvout (pPort, output, inbuf);
    else
    {
        _secVideoUngrabTvout (pPort);

        SECWb *wb = secWbGet ();
        if (wb)
            secWbSetSecure (wb, pPort->secure);
    }

    /* LCD */
    if (output & OUTPUT_LCD)
    {
        SECPtr pSec = SECPTR (pScrn);

        if (pSec->isLcdOff)
            XDBG_TRACE (MVDO, "port(%d) put image after dpms off.\n", pPort->index);
        else if (pPort->inbuf_is_fb)
            lcdout = _secVideoPutImageInbuf (pPort, inbuf);
        else
            lcdout = _secVideoPutImageInternal (pPort, inbuf);
    }

    if (lcdout || tvout)
    {
        ret = Success;
    }
    else
    {
        if (IS_ZEROCOPY (pPort->d.id))
        {
            int i;

            for (i = 0; i < INBUF_NUM; i++)
                if (pPort->inbuf[i] == inbuf)
                {
                    pPort->inbuf[i] = NULL;
                    secUtilRemoveFreeVideoBufferFunc (inbuf, _secVideoFreeInbuf, pPort);
                    break;
                }
            XDBG_WARNING_IF_FAIL (inbuf->ref_cnt == 1);
        }
        else
            XDBG_WARNING_IF_FAIL (inbuf->ref_cnt == 2);

        ret = BadRequest;
    }

    /* decrease ref_cnt here to pass ownership of inbuf to converter or tvout.
     * in case of zero-copy, it will be really freed
     * when converting is finished or tvout is finished.
     */
    secUtilVideoBufferUnref (inbuf);

    pPort->old_d = pPort->d;
    pPort->old_output = output;

    XDBG_TRACE (MVDO, "=======================================.. \n");

    return ret;
}

static int
SECVideoDDPutImage (ClientPtr client,
               DrawablePtr pDraw,
               XvPortPtr pPort,
               GCPtr pGC,
               INT16 src_x, INT16 src_y,
               CARD16 src_w, CARD16 src_h,
               INT16 drw_x, INT16 drw_y,
               CARD16 drw_w, CARD16 drw_h,
               XvImagePtr format,
               unsigned char *data, Bool sync, CARD16 width, CARD16 height)
{
    SECVideoPortInfo *info = _port_info (pDraw);
    int ret;

    if (info)
    {
        info->client = client;
        info->pp = pPort;
    }

    ret = ddPutImage (client, pDraw, pPort, pGC,
                      src_x, src_y, src_w, src_h,
                      drw_x, drw_y, drw_w, drw_h,
                      format, data, sync, width, height);

    return ret;
}

static void
SECVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (!exit)
        return;

    XDBG_DEBUG (MVDO, "exit (%d) \n", exit);

    _secVideoStreamOff (pPort);

    pPort->preemption = 0;
    pPort->rotate = 0;
    pPort->hflip = 0;
    pPort->vflip = 0;
    pPort->punched = FALSE;
}

/**
 * Set up all our internal structures.
 */
static XF86VideoAdaptorPtr
secVideoSetupImageVideo (ScreenPtr pScreen)
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

    pAdaptor->type = XvWindowMask | XvPixmapMask | XvInputMask | XvImageMask;
    pAdaptor->flags = VIDEO_OVERLAID_IMAGES;
    pAdaptor->name = "SEC supporting Software Video Conversions";
    pAdaptor->nEncodings = sizeof (dummy_encoding) / sizeof (XF86VideoEncodingRec);
    pAdaptor->pEncodings = dummy_encoding;
    pAdaptor->nFormats = NUM_FORMATS;
    pAdaptor->pFormats = formats;
    pAdaptor->nPorts = SEC_MAX_PORT;
    pAdaptor->pPortPrivates = (DevUnion*)(&pAdaptor[1]);

    pPort =
        (SECPortPrivPtr) (&pAdaptor->pPortPrivates[SEC_MAX_PORT]);

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        pAdaptor->pPortPrivates[i].ptr = &pPort[i];
        pPort[i].index = i;
        pPort[i].usr_output = OUTPUT_LCD|OUTPUT_EXT;
        pPort[i].outbuf_cvting = -1;
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;
    pAdaptor->nImages = NUM_IMAGES;
    pAdaptor->pImages = images;

    pAdaptor->GetPortAttribute     = SECVideoGetPortAttribute;
    pAdaptor->SetPortAttribute     = SECVideoSetPortAttribute;
    pAdaptor->QueryBestSize        = SECVideoQueryBestSize;
    pAdaptor->QueryImageAttributes = SECVideoQueryImageAttributes;
    pAdaptor->PutImage             = SECVideoPutImage;
    pAdaptor->StopVideo            = SECVideoStop;

    if (!_secVideoRegisterEventResourceTypes ())
    {
        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
        xf86DrvMsg (pScrn->scrnIndex, X_ERROR, "Failed to register EventResourceTypes. \n");
        return FALSE;
    }

    return pAdaptor;
}

static void
SECVideoReplacePutImageFunc (ScreenPtr pScreen)
{
    int i;

    XvScreenPtr xvsp = dixLookupPrivate (&pScreen->devPrivates,
                                         XvGetScreenKey());
    if (!xvsp)
        return;

    for (i = 0; i < xvsp->nAdaptors; i++)
    {
        XvAdaptorPtr pAdapt = xvsp->pAdaptors + i;
        if (pAdapt->ddPutImage)
        {
            ddPutImage = pAdapt->ddPutImage;
            pAdapt->ddPutImage = SECVideoDDPutImage;
            break;
        }
    }

    if (!dixRegisterPrivateKey (VideoPortKey, PRIVATE_WINDOW, sizeof (SECVideoPortInfo)))
        return;
    if (!dixRegisterPrivateKey (VideoPortKey, PRIVATE_PIXMAP, sizeof (SECVideoPortInfo)))
        return;
}

#ifdef XV
/**
 * Set up everything we need for Xv.
 */
Bool secVideoInit (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    SECVideoPrivPtr pVideo;

    pVideo = (SECVideoPrivPtr)calloc (sizeof (SECVideoPriv), 1);
    if (!pVideo)
        return FALSE;

    pVideo->pAdaptor[0] = secVideoSetupImageVideo (pScreen);
    if (!pVideo->pAdaptor[0])
    {
        free (pVideo);
        return FALSE;
    }

    pVideo->pAdaptor[1] = secVideoSetupVirtualVideo (pScreen);
    if (!pVideo->pAdaptor[1])
    {
        free (pVideo->pAdaptor[0]);
        free (pVideo);
        return FALSE;
    }

    pVideo->pAdaptor[2] = secVideoSetupDisplayVideo (pScreen);
    if (!pVideo->pAdaptor[2])
    {
        free (pVideo->pAdaptor[1]);
        free (pVideo->pAdaptor[0]);
        free (pVideo);
        return FALSE;
    }

    xf86XVScreenInit (pScreen, pVideo->pAdaptor, ADAPTOR_NUM);

    SECVideoReplacePutImageFunc (pScreen);
    secVirtualVideoReplacePutStillFunc (pScreen);

    if(registered_handler == FALSE)
    {
        RegisterBlockAndWakeupHandlers(_secVideoBlockHandler,
                                       (WakeupHandlerProcPtr)NoopDDA, pScrn);
        registered_handler = TRUE;
    }

    pSec->pVideoPriv = pVideo;
    xorg_list_init (&layer_owners);

    return TRUE;
}

/**
 * Shut down Xv, used on regeneration.
 */
void secVideoFini (ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    SECVideoPrivPtr pVideo = pSec->pVideoPriv;
    SECPortPrivPtr pCur = NULL, pNext = NULL;
    int i;

    xorg_list_for_each_entry_safe (pCur, pNext, &layer_owners, link)
    {
        if (pCur->tv)
        {
            secVideoTvDisconnect (pCur->tv);
            pCur->tv = NULL;
        }

        if (pCur->d.clip_boxes)
        {
            RegionDestroy (pCur->d.clip_boxes);
            pCur->d.clip_boxes = NULL;
        }
    }

    for (i = 0; i < ADAPTOR_NUM; i++)
        if (pVideo->pAdaptor[i])
            free (pVideo->pAdaptor[i]);

    free (pVideo);
    pSec->pVideoPriv= NULL;
}

#endif

void
secVideoDpms (ScrnInfoPtr pScrn, Bool on)
{
    if (!on)
    {
        SECPtr pSec = (SECPtr) pScrn->driverPrivate;
        XF86VideoAdaptorPtr pAdaptor = pSec->pVideoPriv->pAdaptor[0];
        int i;

        for (i = 0; i < SEC_MAX_PORT; i++)
        {
            SECPortPrivPtr pPort = (SECPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;
            if (pPort->stream_cnt == 0)
                continue;
            XDBG_TRACE (MVDO, "port(%d) cvt stop.\n", pPort->index);
            _secVideoCloseConverter (pPort);
            _secVideoCloseInBuffer (pPort);
        }
    }
}

void
secVideoScreenRotate (ScrnInfoPtr pScrn, int degree)
{
    SECPtr pSec = SECPTR(pScrn);
    SECVideoPrivPtr pVideo = pSec->pVideoPriv;
    int old_degree;

    if (pVideo->screen_rotate_degree == degree)
        return;

    old_degree = pVideo->screen_rotate_degree;
    pVideo->screen_rotate_degree = degree;
    XDBG_DEBUG (MVDO, "screen rotate degree: %d\n", degree);

    if (pSec->isLcdOff)
        return;

    SECPortPrivPtr pCur = NULL, pNext = NULL;
    xorg_list_for_each_entry_safe (pCur, pNext, &layer_owners, link)
    {
        SECModePtr pSecMode = pSec->pSecMode;
        SECVideoBuf *old_vbuf, *rot_vbuf;
        xRectangle rot_rect, dst_rect;
        int rot_width, rot_height;
        int scn_width, scn_height;
        int degree_diff = degree - old_degree;

        if (!pCur->layer)
            continue;

        old_vbuf = secLayerGetBuffer (pCur->layer);
        XDBG_RETURN_IF_FAIL (old_vbuf != NULL);

        rot_width = old_vbuf->width;
        rot_height = old_vbuf->height;
        rot_rect = old_vbuf->crop;
        secUtilRotateArea (&rot_width, &rot_height, &rot_rect, degree_diff);

        rot_vbuf = secUtilAllocVideoBuffer (pScrn, FOURCC_RGB32, rot_width, rot_height,
                                         (pSec->scanout)?TRUE:FALSE, FALSE, pCur->secure);
        XDBG_RETURN_IF_FAIL (rot_vbuf != NULL);
        rot_vbuf->crop = rot_rect;

        secUtilConvertBos (pScrn,
                           old_vbuf->bo[0], old_vbuf->width, old_vbuf->height, &old_vbuf->crop, old_vbuf->width*4,
                           rot_vbuf->bo[0], rot_vbuf->width, rot_vbuf->height, &rot_vbuf->crop, rot_vbuf->width*4,
                           FALSE, degree_diff);

        tbm_bo_map (rot_vbuf->bo[0], TBM_DEVICE_2D, TBM_OPTION_READ);
        tbm_bo_unmap (rot_vbuf->bo[0]);

        secLayerGetRect (pCur->layer, NULL, &dst_rect);

        scn_width = (old_degree % 180)?pSecMode->main_lcd_mode.vdisplay:pSecMode->main_lcd_mode.hdisplay;
        scn_height = (old_degree % 180)?pSecMode->main_lcd_mode.hdisplay:pSecMode->main_lcd_mode.vdisplay;

        secUtilRotateRect (scn_width, scn_height, &dst_rect, degree_diff);

        secLayerFreezeUpdate (pCur->layer, TRUE);
        secLayerSetRect (pCur->layer, &rot_vbuf->crop, &dst_rect);
        secLayerFreezeUpdate (pCur->layer, FALSE);
        secLayerSetBuffer (pCur->layer, rot_vbuf);

        secUtilVideoBufferUnref (rot_vbuf);

        _secVideoCloseConverter (pCur);
    }
}

void
secVideoSwapLayers (ScreenPtr pScreen)
{
    SECPortPrivPtr pCur = NULL, pNext = NULL;
    SECPortPrivPtr pPort1 = NULL, pPort2 = NULL;

    xorg_list_for_each_entry_safe (pCur, pNext, &layer_owners, link)
    {
        if (!pPort1)
            pPort1 = pCur;
        else if (!pPort2)
            pPort2 = pCur;
    }

    if (pPort1 && pPort2)
    {
        secLayerSwapPos (pPort1->layer, pPort2->layer);
        XDBG_TRACE (MVDO, "%p : %p \n", pPort1->layer, pPort2->layer);
    }
}

Bool
secVideoIsSecureMode (ScrnInfoPtr pScrn)
{
    SECPtr pSec = (SECPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pSec->pVideoPriv->pAdaptor[0];
    int i;

    for (i = 0; i < SEC_MAX_PORT; i++)
    {
        SECPortPrivPtr pPort = (SECPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;
        if (pPort->secure)
        {
            XDBG_TRACE (MVDO, "pPort(%d) is secure.\n", pPort->index);
            return TRUE;
        }
    }

    XDBG_TRACE (MVDO, "no secure port.\n");

    return FALSE;
}
