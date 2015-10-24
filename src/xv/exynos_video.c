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

#include "exynos.h"

#include "exynos_accel.h"
#include "exynos_display.h"
#include "exynos_crtc.h"
#include "exynos_output.h"
#include "exynos_video.h"
#include "exynos_prop.h"
#include "exynos_util.h"
#include "exynos_wb.h"
#include "exynos_video_virtual.h"
#include "exynos_video_display.h"
#include "exynos_video_clone.h"
#include "exynos_video_tvout.h"
#include "exynos_video_fourcc.h"
#include "exynos_converter.h"
#include "exynos_plane.h"
#include "exynos_xberc.h"
#include "xv_types.h"
#ifdef LAYER_MANAGER
#include "exynos_layer_manager.h"
#endif

#include <exynos/exynos_drm.h>

#define DONT_FILL_ALPHA     -1
#define EXYNOS_MAX_PORT        16

#define INBUF_NUM           6
#define OUTBUF_NUM          3
#define NUM_HW_LAYER        2

#define OUTPUT_LCD   (1 << 0)
#define OUTPUT_EXT   (1 << 1)
#define OUTPUT_FULL  (1 << 8)

#define CHANGED_NONE 0
#define CHANGED_INPUT 1
#define CHANGED_OUTPUT 2
#define CHANGED_ALL 3

static XF86VideoEncodingRec dummy_encoding[] = {
    {0, "XV_IMAGE", -1, -1, {1, 1}},
    {1, "XV_IMAGE", 4224, 4224, {1, 1}},
};

static XF86ImageRec images[] = {
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

static XF86VideoFormatRec formats[] = {
    {16, TrueColor},
    {24, TrueColor},
    {32, TrueColor},
};

static XF86AttributeRec attributes[] = {
    {0, 0, 270, "_USER_WM_PORT_ATTRIBUTE_ROTATION"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_HFLIP"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_VFLIP"},
    {0, -1, 1, "_USER_WM_PORT_ATTRIBUTE_PREEMPTION"},
    {0, 0, OUTPUT_MODE_EXT_ONLY, "_USER_WM_PORT_ATTRIBUTE_OUTPUT"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_SECURE"},
    {0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_CSC_RANGE"},
};

typedef enum {
    PAA_MIN,
    PAA_ROTATION,
    PAA_HFLIP,
    PAA_VFLIP,
    PAA_PREEMPTION,
    PAA_OUTPUT,
    PAA_SECURE,
    PAA_CSC_RANGE,
    PAA_MAX
} EXYNOSPortAttrAtom;

static struct {
    EXYNOSPortAttrAtom paa;
    const char *name;
    Atom atom;
} atoms[] = {
    {
    PAA_ROTATION, "_USER_WM_PORT_ATTRIBUTE_ROTATION", None}, {
    PAA_HFLIP, "_USER_WM_PORT_ATTRIBUTE_HFLIP", None}, {
    PAA_VFLIP, "_USER_WM_PORT_ATTRIBUTE_VFLIP", None}, {
    PAA_PREEMPTION, "_USER_WM_PORT_ATTRIBUTE_PREEMPTION", None}, {
    PAA_OUTPUT, "_USER_WM_PORT_ATTRIBUTE_OUTPUT", None}, {
    PAA_SECURE, "_USER_WM_PORT_ATTRIBUTE_SECURE", None}, {
PAA_CSC_RANGE, "_USER_WM_PORT_ATTRIBUTE_CSC_RANGE", None},};

enum {
    ON_NONE,
    ON_FB,
    ON_WINDOW,
    ON_PIXMAP
};

__attribute__ ((unused))
static char *drawing_type[4] = { "NONE", "FB", "WIN", "PIX" };

typedef struct _PutData {
    unsigned int id;
    int width;
    int height;
    xRectangle src;
    xRectangle dst;
    void *buf;
    Bool sync;
    RegionPtr clip_boxes;
    void *data;
    DrawablePtr pDraw;
} PutData;

/* EXYNOS port information structure */
typedef struct {
    CARD32 prev_time;
    int index;

    /* attributes */
    int rotate;
    int hflip;
    int vflip;
    int preemption;             /* 1:high, 0:default, -1:low */
    Bool exynosure;
    int csc_range;

    Bool old_secure;
    int old_csc_range;
    int old_rotate;
    int old_hflip;
    int old_vflip;

    ScrnInfoPtr pScrn;
    PutData d;
    PutData old_d;

    /* draw inform */
    int drawing;
    int hw_rotate;

    int in_width;
    int in_height;
    xRectangle in_crop;
    EXYNOSVideoBuf *inbuf[INBUF_NUM];
    Bool inbuf_is_fb;

    /* converter */
    EXYNOSCvt *cvt;
#ifdef LAYER_MANAGER
    /* Layer Manager */
    EXYNOSLayerMngClientID lyr_client_id;
    EXYNOSLayerOutput output;
    EXYNOSLayerPos lpos;
    EXYNOSLayerPos old_lpos;
#endif
#ifndef LAYER_MANAGER
    /* layer */
    EXYNOSLayer *layer;
#endif
    int out_width;
    int out_height;
    xRectangle out_crop;
    EXYNOSVideoBuf *outbuf[OUTBUF_NUM];
    int outbuf_cvting;
    DrawablePtr pDamageDrawable[OUTBUF_NUM];

    /* tvout */
    int usr_output;
    int old_output;
    int grab_tvout;
    EXYNOSVideoTv *tv;
    void *gem_list;
    Bool skip_tvout;
    Bool need_start_wb;
    EXYNOSVideoBuf *wait_vbuf;
    CARD32 tv_prev_time;

    /* count */
    unsigned int put_counts;
    OsTimerPtr timer;
    Bool punched;
    int stream_cnt;

    struct xorg_list link;
} EXYNOSPortPriv, *EXYNOSPortPrivPtr;

static RESTYPE event_drawable_type;

typedef struct _EXYNOSVideoResource {
    XID id;
    RESTYPE type;

    EXYNOSPortPrivPtr pPort;
    ScrnInfoPtr pScrn;
} EXYNOSVideoResource;

typedef struct _EXYNOSVideoPortInfo {
    ClientPtr client;
    XvPortPtr pp;
} EXYNOSVideoPortInfo;

static int (*ddPutImage) (ClientPtr, DrawablePtr, struct _XvPortRec *, GCPtr,
                          INT16, INT16, CARD16, CARD16,
                          INT16, INT16, CARD16, CARD16,
                          XvImagePtr, unsigned char *, Bool, CARD16, CARD16);

static void _exynosVideoSendReturnBufferMessage(EXYNOSPortPrivPtr pPort,
                                                EXYNOSVideoBuf * vbuf,
                                                unsigned int *keys);
static void EXYNOSVideoStop(ScrnInfoPtr pScrn, pointer data, Bool exit);
static void _exynosVideoCloseInBuffer(EXYNOSPortPrivPtr pPort);
static void _exynosVideoCloseOutBuffer(EXYNOSPortPrivPtr pPort,
                                       Bool close_layer);
static void _exynosVideoCloseConverter(EXYNOSPortPrivPtr pPort);
static Bool _exynosVideoSetOutputExternalProperty(DrawablePtr pDraw,
                                                  Bool tvout);

static int streaming_ports;
static int registered_handler;
static struct xorg_list layer_owners;

static DevPrivateKeyRec video_port_key;

#define VideoPortKey (&video_port_key)
#define GetPortInfo(pDraw) ((EXYNOSVideoPortInfo*)dixLookupPrivate(&(pDraw)->devPrivates, VideoPortKey))

#define NUM_IMAGES        (sizeof(images) / sizeof(images[0]))
#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

#define ENSURE_AREA(off, lng, max) (lng = ((off + lng) > max ? (max - off) : lng))

static CARD32
_countPrint(OsTimerPtr timer, CARD32 now, pointer arg)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) arg;

    if (pPort->timer) {
        TimerFree(pPort->timer);
        pPort->timer = NULL;
    }

    ErrorF("PutImage(%d) : %d fps. \n", pPort->index, pPort->put_counts);

    pPort->put_counts = 0;

    return 0;
}

static void
_countFps(EXYNOSPortPrivPtr pPort)
{
    pPort->put_counts++;

    if (pPort->timer)
        return;

    pPort->timer = TimerSet(NULL, 0, 1000, _countPrint, pPort);
}

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

static PixmapPtr
_getPixmap(DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pDraw->pScreen->GetWindowPixmap((WindowPtr) pDraw);
    else
        return (PixmapPtr) pDraw;
}

static XF86ImagePtr
_get_image_info(int id)
{
    int i;

    for (i = 0; i < NUM_IMAGES; i++)
        if (images[i].id == id)
            return &images[i];

    return NULL;
}

static Atom
_portAtom(EXYNOSPortAttrAtom paa)
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

    XDBG_ERROR(MVDO, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static void
_DestroyData(void *port, uniType data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) port;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    uint32_t handle = data.u32;

    exynosUtilFreeHandle(pPort->pScrn, handle);
}

static Bool
_exynosVideoGrabTvout(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

    if (pPort->grab_tvout)
        return TRUE;

    /* other port already grabbed */
    if (pVideo->tvout_in_use) {
        XDBG_WARNING(MVDO, "*** pPort(%p) can't grab tvout. It's in use.\n",
                     pPort);
        return FALSE;
    }

    if (pPort->tv) {
        XDBG_ERROR(MVDO, "*** wrong handle if you reach here. %p \n",
                   pPort->tv);
        return FALSE;
    }

    pPort->grab_tvout = TRUE;
    pVideo->tvout_in_use = TRUE;

    XDBG_TRACE(MVDO, "pPort(%p) grabs tvout.\n", pPort);

    return TRUE;
}

static void
_exynosVideoUngrabTvout(EXYNOSPortPrivPtr pPort)
{
    if (pPort->tv) {
        exynosVideoTvDisconnect(pPort->tv);
        pPort->tv = NULL;
    }

    /* This port didn't grab tvout */
    if (!pPort->grab_tvout)
        return;

    _exynosVideoSetOutputExternalProperty(pPort->d.pDraw, FALSE);

    if (pPort->need_start_wb) {
        EXYNOSWb *wb = exynosWbGet();

        if (wb) {
            exynosWbSetSecure(wb, pPort->exynosure);
            exynosWbStart(wb);
        }
        pPort->need_start_wb = FALSE;
    }

    XDBG_TRACE(MVDO, "pPort(%p) ungrabs tvout.\n", pPort);

    pPort->grab_tvout = FALSE;

    if (pPort->pScrn) {
        EXYNOSVideoPrivPtr pVideo;

        pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;
        pVideo->tvout_in_use = FALSE;
    }
    pPort->wait_vbuf = NULL;
}

static int
_exynosVideoGetTvoutMode(EXYNOSPortPrivPtr pPort)
{
    EXYNOSModePtr pExynosMode =
        (EXYNOSModePtr) EXYNOSPTR(pPort->pScrn)->pExynosMode;
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;
    EXYNOSDisplaySetMode disp_mode = exynosDisplayGetDispSetMode(pPort->pScrn);
    int output = OUTPUT_LCD;

#if 0
    if (disp_mode == DISPLAY_SET_MODE_CLONE) {
        if (pPort->preemption > -1) {
            if (pVideo->video_output > 0 && streaming_ports == 1) {
                int video_output = pVideo->video_output - 1;

                if (video_output == OUTPUT_MODE_DEFAULT)
                    output = OUTPUT_LCD;
                else if (video_output == OUTPUT_MODE_TVOUT)
                    output = OUTPUT_LCD | OUTPUT_EXT | OUTPUT_FULL;
                else
                    output = OUTPUT_EXT | OUTPUT_FULL;
            }
            else if (streaming_ports == 1) {
                output = pPort->usr_output;
                if (!(output & OUTPUT_FULL))
                    output &= ~(OUTPUT_EXT);
            }
            else if (streaming_ports > 1)
                output = OUTPUT_LCD;
            else
                XDBG_NEVER_GET_HERE(MVDO);
        }
        else
            output = OUTPUT_LCD;
    }
    else if (disp_mode == DISPLAY_SET_MODE_EXT) {
#else
    if (disp_mode == DISPLAY_SET_MODE_EXT) {
#endif
        if (pPort->drawing == ON_PIXMAP) {
            xf86CrtcPtr pCrtc = exynosCrtcGetAtGeometry(pPort->pScrn,
                                                        (int) pPort->d.pDraw->x,
                                                        (int) pPort->d.pDraw->y,
                                                        (int) pPort->d.pDraw->
                                                        width,
                                                        (int) pPort->d.pDraw->
                                                        height);
            int c = exynosCrtcGetConnectType(pCrtc);

            if (c == DRM_MODE_CONNECTOR_LVDS || c == DRM_MODE_CONNECTOR_Unknown)
                output = OUTPUT_LCD;
            else if (c == DRM_MODE_CONNECTOR_HDMIA ||
                     c == DRM_MODE_CONNECTOR_HDMIB)
                output = OUTPUT_EXT;
            else if (c == DRM_MODE_CONNECTOR_VIRTUAL)
                output = OUTPUT_EXT;
            else
                XDBG_NEVER_GET_HERE(MVDO);
        }
        else {
            xf86CrtcPtr pCrtc = exynosCrtcGetAtGeometry(pPort->pScrn,
                                                        (int) pPort->d.dst.x,
                                                        (int) pPort->d.dst.y,
                                                        (int) pPort->d.dst.
                                                        width,
                                                        (int) pPort->d.dst.
                                                        height);
            int c = exynosCrtcGetConnectType(pCrtc);

            if (c == DRM_MODE_CONNECTOR_LVDS || c == DRM_MODE_CONNECTOR_Unknown)
                output = OUTPUT_LCD;
            else if (c == DRM_MODE_CONNECTOR_HDMIA ||
                     c == DRM_MODE_CONNECTOR_HDMIB)
                output = OUTPUT_EXT;
            else if (c == DRM_MODE_CONNECTOR_VIRTUAL)
                output = OUTPUT_EXT;
            else
                XDBG_NEVER_GET_HERE(MVDO);
        }
    }
    else {                      /* DISPLAY_SET_MODE_OFF */

        output = OUTPUT_LCD;
    }

    /* OUTPUT_LCD is default display. If default display is HDMI,
     * we need to change OUTPUT_LCD to OUTPUT_HDMI
     */

    XDBG_DEBUG(MVDO,
               "drawing(%d) disp_mode(%d) preemption(%d) streaming_ports(%d) conn_mode(%d) usr_output(%d) video_output(%d) output(%x) skip(%d)\n",
               pPort->drawing, disp_mode, pPort->preemption, streaming_ports,
               pExynosMode->conn_mode, pPort->usr_output, pVideo->video_output,
               output, pPort->skip_tvout);

    return output;
}

static int
_exynosVideodrawingOn(EXYNOSPortPrivPtr pPort)
{
    if (pPort->old_d.pDraw != pPort->d.pDraw)
        pPort->drawing = ON_NONE;

    if (pPort->drawing != ON_NONE)
        return pPort->drawing;

    if (pPort->d.pDraw->type == DRAWABLE_PIXMAP)
        return ON_PIXMAP;
    else if (pPort->d.pDraw->type == DRAWABLE_WINDOW) {
        PropertyPtr prop =
            exynosUtilGetWindowProperty((WindowPtr) pPort->d.pDraw,
                                        "XV_ON_DRAWABLE");

        if (prop && *(int *) prop->data > 0)
            return ON_WINDOW;
    }

    return ON_FB;
}

static void
_exynosVideoGetRotation(EXYNOSPortPrivPtr pPort, int *hw_rotate)
{
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

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
_exynosVideoGetKeys(EXYNOSPortPrivPtr pPort, unsigned int *keys,
                    unsigned int *type)
{
    XV_DATA_PTR data = (XV_DATA_PTR) pPort->d.buf;
    int valid = XV_VALIDATE_DATA(data);

    if (valid == XV_HEADER_ERROR) {
        XDBG_ERROR(MVDO, "XV_HEADER_ERROR\n");
        return valid;
    }
    else if (valid == XV_VERSION_MISMATCH) {
        XDBG_ERROR(MVDO, "XV_VERSION_MISMATCH\n");
        return valid;
    }

    if (keys) {
        keys[0] = data->YBuf;
        keys[1] = data->CbBuf;
        keys[2] = data->CrBuf;
    }

    if (type)
        *type = data->BufType;

    return 0;
}

static void
_exynosVideoFreeInbuf(EXYNOSVideoBuf * vbuf, void *data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    int i;

    XDBG_RETURN_IF_FAIL(pPort->drawing != ON_NONE);

    for (i = 0; i < INBUF_NUM; i++)
        if (pPort->inbuf[i] == vbuf) {
            _exynosVideoSendReturnBufferMessage(pPort, vbuf, NULL);
            pPort->inbuf[i] = NULL;
            return;
        }

    XDBG_NEVER_GET_HERE(MVDO);
}

static void
_exynosVideoFreeOutbuf(EXYNOSVideoBuf * vbuf, void *data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    int i;

    XDBG_RETURN_IF_FAIL(pPort->drawing != ON_NONE);

    for (i = 0; i < OUTBUF_NUM; i++)
        if (pPort->outbuf[i] == vbuf) {
            pPort->pDamageDrawable[i] = NULL;
            pPort->outbuf[i] = NULL;
            return;
        }

    XDBG_NEVER_GET_HERE(MVDO);
}

#ifndef LAYER_MANAGER
static EXYNOSLayer *
#else
static Bool
#endif
_exynosVideoCreateLayer(EXYNOSPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;

#ifndef LAYER_MANAGER
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pScrn)->pVideoPriv;
    xRectangle src, dst;
    EXYNOSLayer *layer;
    Bool full = FALSE;
#endif
    DrawablePtr pDraw = pPort->d.pDraw;
    xf86CrtcConfigPtr pCrtcConfig;
    xf86OutputPtr pOutput = NULL;
    int i;
    xf86CrtcPtr pCrtc;

    pCrtc =
        exynosCrtcGetAtGeometry(pScrn, pDraw->x, pDraw->y, pDraw->width,
                                pDraw->height);
#ifndef LAYER_MANAGER
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, NULL);
    pCrtcConfig = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    XDBG_RETURN_VAL_IF_FAIL(pCrtcConfig != NULL, NULL);
#else
    XDBG_RETURN_VAL_IF_FAIL(pCrtc != NULL, FALSE);
    pCrtcConfig = XF86_CRTC_CONFIG_PTR(pCrtc->scrn);
    XDBG_RETURN_VAL_IF_FAIL(pCrtcConfig != NULL, FALSE);
#endif

    for (i = 0; i < pCrtcConfig->num_output; i++) {
        xf86OutputPtr pTemp = pCrtcConfig->output[i];

        if (pTemp->crtc == pCrtc) {
            pOutput = pTemp;
            break;
        }
    }
#ifndef LAYER_MANAGER
    XDBG_RETURN_VAL_IF_FAIL(pOutput != NULL, NULL);
#else
    XDBG_RETURN_VAL_IF_FAIL(pOutput != NULL, FALSE);
#endif

    EXYNOSOutputPrivPtr pOutputPriv = pOutput->driver_private;
    EXYNOSLayerOutput output = LAYER_OUTPUT_LCD;

    if (pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_LVDS ||
        pOutputPriv->mode_output->connector_type == DRM_MODE_CONNECTOR_Unknown)
    {
        output = LAYER_OUTPUT_LCD;
    }
    else if (pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_HDMIA ||
             pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_HDMIB ||
             pOutputPriv->mode_output->connector_type ==
             DRM_MODE_CONNECTOR_VIRTUAL) {
        output = LAYER_OUTPUT_EXT;
    }
    else
        XDBG_NEVER_GET_HERE(MVDO);
#ifndef LAYER_MANAGER
    if (exynosLayerFind(output, LAYER_LOWER2) &&
        exynosLayerFind(output, LAYER_LOWER1))
        full = TRUE;
    if (full) {
        return NULL;
    }
#else
    EXYNOSLayerPos p_lpos[PLANE_MAX];
    int count_available_layer =
        exynosLayerMngGetListOfAccessablePos(pPort->lyr_client_id, output,
                                             p_lpos);
    if (count_available_layer == 0) {
        XDBG_ERROR(MVDO, "XV Layers is busy\n");
        return FALSE;
    }
#endif

#ifndef LAYER_MANAGER
    layer = exynosLayerCreate(pScrn, output, LAYER_NONE);
    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, NULL);

    src = dst = pPort->out_crop;
    dst.x = pPort->d.dst.x;
    dst.y = pPort->d.dst.y;

    exynosLayerSetRect(layer, &src, &dst);
    exynosLayerEnableVBlank(layer, TRUE);
    exynosLayerSetOffset(layer, pVideo->video_offset_x, pVideo->video_offset_y);
#else
    pPort->output = output;
    for (i = 0; i < count_available_layer; i++) {
        if (exynosLayerMngReservation(pPort->lyr_client_id, output, p_lpos[i])) {
            pPort->lpos = p_lpos[i];
            break;
        }
    }
    if (pPort->lpos == LAYER_NONE)
        return FALSE;
#endif
    xorg_list_add(&pPort->link, &layer_owners);

#ifndef LAYER_MANAGER
    return layer;
#else
    return TRUE;
#endif
}

static EXYNOSVideoBuf *
_exynosVideoGetInbufZeroCopy(EXYNOSPortPrivPtr pPort, unsigned int *names,
                             unsigned int buf_type)
{
    EXYNOSVideoBuf *inbuf = NULL;
    int i, empty;
    tbm_bo_handle bo_handle;

    for (empty = 0; empty < INBUF_NUM; empty++)
        if (!pPort->inbuf[empty])
            break;

    if (empty == INBUF_NUM) {
        XDBG_ERROR(MVDO, "now all inbufs in use!\n");
        return NULL;
    }

    /* make sure both widths are same. */
    XDBG_RETURN_VAL_IF_FAIL(pPort->d.width == pPort->in_width, NULL);
    XDBG_RETURN_VAL_IF_FAIL(pPort->d.height == pPort->in_height, NULL);

    inbuf = exynosUtilCreateVideoBuffer(pPort->pScrn, pPort->d.id,
                                        pPort->in_width, pPort->in_height,
                                        pPort->exynosure);
    XDBG_RETURN_VAL_IF_FAIL(inbuf != NULL, NULL);

    inbuf->crop = pPort->in_crop;

    for (i = 0; i < PLANAR_CNT; i++) {
        if (names[i] > 0) {
            inbuf->keys[i] = names[i];

            if (buf_type == XV_BUF_TYPE_LEGACY) {
#ifdef LEGACY_INTERFACE
                uniType data =
                    exynosUtilListGetData(pPort->gem_list, (void *) names[i]);
                if (!data.ptr) {
                    exynosUtilConvertPhyaddress(pPort->pScrn, names[i],
                                                inbuf->lengths[i],
                                                &inbuf->handles[i]);

                    pPort->gem_list =
                        exynosUtilListAdd(pPort->gem_list, (void *) names[i],
                                          setunitype32(inbuf->handles[i]));
                }
                else
                    inbuf->handles[i] = data.u32;

                XDBG_DEBUG(MVDO, "%d, %p => %u \n", i, (void *) names[i],
                           inbuf->handles[i]);
#else
                XDBG_ERROR(MVDO, "not support legacy type\n");
                goto fail_dma;
#endif
            }
            else {
                XDBG_GOTO_IF_FAIL(inbuf->lengths[i] > 0, fail_dma);
                XDBG_GOTO_IF_FAIL(inbuf->bo[i] == NULL, fail_dma);

                inbuf->bo[i] =
                    tbm_bo_import(EXYNOSPTR(pPort->pScrn)->tbm_bufmgr,
                                  inbuf->keys[i]);
                XDBG_GOTO_IF_FAIL(inbuf->bo[i] != NULL, fail_dma);

                bo_handle = tbm_bo_get_handle(inbuf->bo[i], TBM_DEVICE_DEFAULT);
                inbuf->handles[i] = bo_handle.u32;
                XDBG_GOTO_IF_FAIL(inbuf->handles[i] > 0, fail_dma);

                inbuf->offsets[i] = 0;

                XDBG_DEBUG(MVDO, "%d, key(%d) => bo(%p) handle(%d)\n",
                           i, inbuf->keys[i], inbuf->bo[i], inbuf->handles[i]);
            }
        }
    }

    /* not increase ref_cnt to free inbuf when converting/showing is done. */
    pPort->inbuf[empty] = inbuf;

    exynosUtilAddFreeVideoBufferFunc(inbuf, _exynosVideoFreeInbuf, pPort);

    return inbuf;

 fail_dma:
    if (inbuf)
        exynosUtilFreeVideoBuffer(inbuf);

    return NULL;
}

static EXYNOSVideoBuf *
_exynosVideoGetInbufRAW(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoBuf *inbuf = NULL;
    void *vir_addr = NULL;
    int i;
    tbm_bo_handle bo_handle;

    /* we can't access virtual pointer. */
    XDBG_RETURN_VAL_IF_FAIL(pPort->exynosure == FALSE, NULL);

    for (i = 0; i < INBUF_NUM; i++) {
        if (pPort->inbuf[i])
            continue;

        pPort->inbuf[i] = exynosUtilAllocVideoBuffer(pPort->pScrn, pPort->d.id,
                                                     pPort->in_width,
                                                     pPort->in_height, FALSE,
                                                     FALSE, pPort->exynosure);
        XDBG_GOTO_IF_FAIL(pPort->inbuf[i] != NULL, fail_raw_alloc);
    }

    for (i = 0; i < INBUF_NUM; i++) {
        XDBG_DEBUG(MVDO, "? inbuf(%d,%p) converting(%d) showing(%d)\n", i,
                   pPort->inbuf[i], VBUF_IS_CONVERTING(pPort->inbuf[i]),
                   pPort->inbuf[i]->showing);

        if (pPort->inbuf[i] && !VBUF_IS_CONVERTING(pPort->inbuf[i]) &&
            !pPort->inbuf[i]->showing) {
            /* increase ref_cnt to keep inbuf until stream_off. */
            inbuf = exynosUtilVideoBufferRef(pPort->inbuf[i]);
            break;
        }
    }

    if (!inbuf) {
        XDBG_ERROR(MVDO, "now all inbufs in use!\n");
        return NULL;
    }

    inbuf->crop = pPort->in_crop;

    bo_handle = tbm_bo_map(inbuf->bo[0], TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    vir_addr = bo_handle.ptr;
    XDBG_RETURN_VAL_IF_FAIL(vir_addr != NULL, NULL);
    XDBG_RETURN_VAL_IF_FAIL(inbuf->size > 0, NULL);

    if (pPort->d.width != pPort->in_width ||
        pPort->d.height != pPort->in_height) {
        XF86ImagePtr image_info = _get_image_info(pPort->d.id);

        XDBG_RETURN_VAL_IF_FAIL(image_info != NULL, NULL);
        int pitches[3] = { 0, };
        int offsets[3] = { 0, };
        int lengths[3] = { 0, };
        int width, height;

        width = pPort->d.width;
        height = pPort->d.height;

        exynosVideoQueryImageAttrs(pPort->pScrn, pPort->d.id,
                                   &width, &height, pitches, offsets, lengths);

        exynosUtilCopyImage(width, height,
                            pPort->d.buf, width, height,
                            pitches, offsets, lengths,
                            vir_addr, inbuf->width, inbuf->height,
                            inbuf->pitches, inbuf->offsets, inbuf->lengths,
                            image_info->num_planes,
                            image_info->horz_u_period,
                            image_info->vert_u_period);
    }
    else
        memcpy(vir_addr, pPort->d.buf, inbuf->size);

    tbm_bo_unmap(inbuf->bo[0]);
    exynosUtilCacheFlush(pPort->pScrn);
    return inbuf;

 fail_raw_alloc:
    _exynosVideoCloseInBuffer(pPort);
    return NULL;
}

static EXYNOSVideoBuf *
_exynosVideoGetInbuf(EXYNOSPortPrivPtr pPort)
{
    unsigned int keys[PLANAR_CNT] = { 0, };
    unsigned int buf_type = 0;
    EXYNOSVideoBuf *inbuf = NULL;
    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    TTRACE_VIDEO_BEGIN("XORG:XV:GETINBUF");

    if (IS_ZEROCOPY(pPort->d.id)) {
        if (_exynosVideoGetKeys(pPort, keys, &buf_type)) {
            TTRACE_VIDEO_END();
            return NULL;
        }

        XDBG_GOTO_IF_FAIL(keys[0] > 0, inbuf_fail);

        if (pPort->d.id == FOURCC_SN12 || pPort->d.id == FOURCC_ST12)
            XDBG_GOTO_IF_FAIL(keys[1] > 0, inbuf_fail);

        inbuf = _exynosVideoGetInbufZeroCopy(pPort, keys, buf_type);

        XDBG_TRACE(MVDO, "keys: %d,%d,%d. stamp(%" PRIuPTR ")\n", keys[0],
                   keys[1], keys[2], VSTMAP(inbuf));
    }
    else
        inbuf = _exynosVideoGetInbufRAW(pPort);

    XDBG_GOTO_IF_FAIL(inbuf != NULL, inbuf_fail);

    if ((pExynos->dump_mode & XBERC_DUMP_MODE_IA) && pExynos->dump_info) {
        char file[128];
        static int i;

        snprintf(file, sizeof(file), "xvin_%c%c%c%c_%dx%d_p%d_%03d.%s",
                 FOURCC_STR(inbuf->id),
                 inbuf->width, inbuf->height, pPort->index, i++,
                 IS_RGB(inbuf->id) ? "bmp" : "yuv");
        exynosUtilDoDumpVBuf(pExynos->dump_info, inbuf, file);
    }

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_IA)
        inbuf->put_time = GetTimeInMillis();

    TTRACE_VIDEO_END();

    return inbuf;

 inbuf_fail:
    TTRACE_VIDEO_END();
    return NULL;
}

static EXYNOSVideoBuf *
_exynosVideoGetOutbufDrawable(EXYNOSPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    DrawablePtr pDraw = pPort->d.pDraw;
    PixmapPtr pPixmap = (PixmapPtr) _getPixmap(pDraw);
    EXYNOSPixmapPriv *privPixmap = exaGetPixmapDriverPrivate(pPixmap);
    Bool need_finish = FALSE;
    EXYNOSVideoBuf *outbuf = NULL;
    int empty;
    tbm_bo_handle bo_handle;

    for (empty = 0; empty < OUTBUF_NUM; empty++)
        if (!pPort->outbuf[empty])
            break;

    if (empty == OUTBUF_NUM) {
        XDBG_ERROR(MVDO, "now all outbufs in use!\n");
        return NULL;
    }

    if ((pDraw->width % 16) &&
        (pPixmap->usage_hint != CREATE_PIXMAP_USAGE_XVIDEO)) {
        ScreenPtr pScreen = pScrn->pScreen;
        EXYNOSFbBoDataPtr bo_data = NULL;

        pPixmap->usage_hint = CREATE_PIXMAP_USAGE_XVIDEO;
        pScreen->ModifyPixmapHeader(pPixmap,
                                    pDraw->width, pDraw->height,
                                    pDraw->depth,
                                    pDraw->bitsPerPixel, pPixmap->devKind, 0);
        XDBG_RETURN_VAL_IF_FAIL(privPixmap->bo != NULL, NULL);

        tbm_bo_get_user_data(privPixmap->bo, TBM_BO_DATA_FB,
                             (void **) &bo_data);
        XDBG_RETURN_VAL_IF_FAIL(bo_data != NULL, NULL);
        XDBG_RETURN_VAL_IF_FAIL((bo_data->pos.x2 - bo_data->pos.x1) ==
                                pPort->out_width, NULL);
        XDBG_RETURN_VAL_IF_FAIL((bo_data->pos.y2 - bo_data->pos.y1) ==
                                pPort->out_height, NULL);
    }

    if (!privPixmap->bo) {
        need_finish = TRUE;
        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL(privPixmap->bo != NULL, fail_drawable);
    }

    outbuf = exynosUtilCreateVideoBuffer(pScrn, FOURCC_RGB32,
                                         pPort->out_width,
                                         pPort->out_height, pPort->exynosure);
    XDBG_GOTO_IF_FAIL(outbuf != NULL, fail_drawable);
    outbuf->crop = pPort->out_crop;

    XDBG_TRACE(MVDO, "outbuf(%p)(%dx%d) created. [%s]\n",
               outbuf, pPort->out_width, pPort->out_height,
               (pPort->drawing == ON_PIXMAP) ? "PIX" : "WIN");

    outbuf->bo[0] = tbm_bo_ref(privPixmap->bo);

    bo_handle = tbm_bo_get_handle(outbuf->bo[0], TBM_DEVICE_DEFAULT);
    outbuf->handles[0] = bo_handle.u32;
    XDBG_GOTO_IF_FAIL(outbuf->handles[0] > 0, fail_drawable);

    if (need_finish)
        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

    pPort->pDamageDrawable[empty] = pPort->d.pDraw;

//    RegionTranslate (pPort->d.clip_boxes, -pPort->d.dst.x, -pPort->d.dst.y);

    /* not increase ref_cnt to free outbuf when converting/showing is done. */
    pPort->outbuf[empty] = outbuf;

    exynosUtilAddFreeVideoBufferFunc(outbuf, _exynosVideoFreeOutbuf, pPort);

    return outbuf;

 fail_drawable:
    if (outbuf)
        exynosUtilFreeVideoBuffer(outbuf);

    return NULL;
}

static EXYNOSVideoBuf *
_exynosVideoGetOutbufFB(EXYNOSPortPrivPtr pPort)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    EXYNOSVideoBuf *outbuf = NULL;
    int i, next;

#ifndef LAYER_MANAGER
    if (!pPort->layer) {
        pPort->layer = _exynosVideoCreateLayer(pPort);
        XDBG_RETURN_VAL_IF_FAIL(pPort->layer != NULL, NULL);
    }
    else {
        EXYNOSVideoBuf *vbuf = exynosLayerGetBuffer(pPort->layer);

        if (vbuf &&
            (vbuf->width == pPort->out_width &&
             vbuf->height == pPort->out_height)) {
            xRectangle src = { 0, }, dst = {
            0,};

            exynosLayerGetRect(pPort->layer, &src, &dst);

            /* CHECK */
            if (pPort->d.dst.x != dst.x || pPort->d.dst.y != dst.y) {
                /* x,y can be changed when window is moved. */
                dst.x = pPort->d.dst.x;
                dst.y = pPort->d.dst.y;
                exynosLayerSetRect(pPort->layer, &src, &dst);
            }
        }
    }
#else
    if (pPort->lpos == LAYER_NONE) {
        XDBG_RETURN_VAL_IF_FAIL(_exynosVideoCreateLayer(pPort), NULL);
    }
    else if (pPort->lpos != pPort->old_lpos) {
        exynosLayerMngRelease(pPort->lyr_client_id, pPort->output,
                              pPort->old_lpos);
    }
#endif

    for (i = 0; i < OUTBUF_NUM; i++) {
        EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

        if (pPort->outbuf[i])
            continue;

        pPort->outbuf[i] = exynosUtilAllocVideoBuffer(pScrn, FOURCC_RGB32,
                                                      pPort->out_width,
                                                      pPort->out_height,
                                                      (pExynos->
                                                       scanout) ? TRUE : FALSE,
                                                      FALSE, pPort->exynosure);
        XDBG_GOTO_IF_FAIL(pPort->outbuf[i] != NULL, fail_fb);
        pPort->outbuf[i]->crop = pPort->out_crop;

        XDBG_TRACE(MVDO, "out bo(%p, %d, %dx%d) created. [FB]\n",
                   pPort->outbuf[i]->bo[0], pPort->outbuf[i]->handles[0],
                   pPort->out_width, pPort->out_height);
    }

    next = ++pPort->outbuf_cvting;
    if (next >= OUTBUF_NUM)
        next = 0;

    for (i = 0; i < OUTBUF_NUM; i++) {
        XDBG_DEBUG(MVDO, "? outbuf(%d,%p) converting(%d)\n", next,
                   pPort->outbuf[next],
                   VBUF_IS_CONVERTING(pPort->outbuf[next]));

        if (pPort->outbuf[next] && !VBUF_IS_CONVERTING(pPort->outbuf[next])) {
            /* increase ref_cnt to keep outbuf until stream_off. */
            outbuf = exynosUtilVideoBufferRef(pPort->outbuf[next]);
            break;
        }

        next++;
        if (next >= OUTBUF_NUM)
            next = 0;
    }

    if (!outbuf) {
        XDBG_ERROR(MVDO, "now all outbufs in use!\n");
        return NULL;
    }

    pPort->outbuf_cvting = next;

    return outbuf;

 fail_fb:
    _exynosVideoCloseConverter(pPort);
    _exynosVideoCloseOutBuffer(pPort, TRUE);

    return NULL;
}

static EXYNOSVideoBuf *
_exynosVideoGetOutbuf(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoBuf *outbuf;

    TTRACE_VIDEO_BEGIN("XORG:XV:GETOUTBUF");

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        outbuf = _exynosVideoGetOutbufDrawable(pPort);
    else                        /* ON_FB */
        outbuf = _exynosVideoGetOutbufFB(pPort);

    TTRACE_VIDEO_END();

    return outbuf;
}

static void
_exynosVideoCloseInBuffer(EXYNOSPortPrivPtr pPort)
{
    int i;

    if (pPort->gem_list) {
        exynosUtilListDestroyData(pPort->gem_list, _DestroyData, pPort);
        exynosUtilListDestroy(pPort->gem_list);
        pPort->gem_list = NULL;
    }

    if (!IS_ZEROCOPY(pPort->d.id))
        for (i = 0; i < INBUF_NUM; i++) {
            if (pPort->inbuf[i]) {
                exynosUtilVideoBufferUnref(pPort->inbuf[i]);
                pPort->inbuf[i] = NULL;
            }
        }

    pPort->in_width = 0;
    pPort->in_height = 0;
    memset(&pPort->in_crop, 0, sizeof(xRectangle));

    XDBG_DEBUG(MVDO, "done\n");
}

static void
_exynosVideoCloseOutBuffer(EXYNOSPortPrivPtr pPort, Bool close_layer)
{
    int i;

    /* before close outbuf, layer/cvt should be finished. */
#ifndef LAYER_MANAGER
    if (close_layer && pPort->layer) {
        exynosLayerUnref(pPort->layer);
        pPort->layer = NULL;
        xorg_list_del(&pPort->link);
    }
#else
    if (close_layer && pPort->lpos != LAYER_NONE) {
        xorg_list_del(&pPort->link);
        exynosLayerMngRelease(pPort->lyr_client_id, pPort->output, pPort->lpos);
        pPort->lpos = LAYER_NONE;
    }
#endif
    for (i = 0; i < OUTBUF_NUM; i++) {
        if (pPort->outbuf[i]) {
            if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
                XDBG_NEVER_GET_HERE(MVDO);

            exynosUtilVideoBufferUnref(pPort->outbuf[i]);
            pPort->outbuf[i] = NULL;
        }
    }

    pPort->out_width = 0;
    pPort->out_height = 0;
    memset(&pPort->out_crop, 0, sizeof(xRectangle));
    pPort->outbuf_cvting = -1;

    XDBG_DEBUG(MVDO, "done\n");
}

static void
_exynosVideoSendReturnBufferMessage(EXYNOSPortPrivPtr pPort,
                                    EXYNOSVideoBuf * vbuf, unsigned int *keys)
{
    static Atom return_atom = None;
    EXYNOSVideoPortInfo *info = _port_info(pPort->d.pDraw);

    if (return_atom == None)
        return_atom = MakeAtom("XV_RETURN_BUFFER",
                               strlen("XV_RETURN_BUFFER"), TRUE);

    if (!info)
        return;

    xEvent event;

    CLEAR(event);
    event.u.u.type = ClientMessage;
    event.u.u.detail = 32;
    event.u.clientMessage.u.l.type = return_atom;
    if (vbuf) {
        event.u.clientMessage.u.l.longs0 = (INT32) vbuf->keys[0];
        event.u.clientMessage.u.l.longs1 = (INT32) vbuf->keys[1];
        event.u.clientMessage.u.l.longs2 = (INT32) vbuf->keys[2];

        XDBG_TRACE(MVDO, "%" PRIuPTR ": %d,%d,%d out. diff(%" PRId64 ")\n",
                   vbuf->stamp, vbuf->keys[0], vbuf->keys[1], vbuf->keys[2],
                   (int64_t) GetTimeInMillis() - (int64_t) vbuf->stamp);
    }
    else if (keys) {
        event.u.clientMessage.u.l.longs0 = (INT32) keys[0];
        event.u.clientMessage.u.l.longs1 = (INT32) keys[1];
        event.u.clientMessage.u.l.longs2 = (INT32) keys[2];

        XDBG_TRACE(MVDO, "%d,%d,%d out. \n", keys[0], keys[1], keys[2]);
    }
    else
        XDBG_NEVER_GET_HERE(MVDO);

    WriteEventsToClient(info->client, 1, (xEventPtr) & event);

    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_IA) {
        if (vbuf) {
            CARD32 cur, sub;

            cur = GetTimeInMillis();
            sub = cur - vbuf->put_time;
            ErrorF("vbuf(%d,%d,%d)         retbuf  : %6" PRIXID " ms\n",
                   vbuf->keys[0], vbuf->keys[1], vbuf->keys[2], sub);
        }
        else if (keys)
            ErrorF("vbuf(%d,%d,%d)         retbuf  : 0 ms\n",
                   keys[0], keys[1], keys[2]);
        else
            XDBG_NEVER_GET_HERE(MVDO);
    }
}

#ifdef LAYER_MANAGER
static void
_exynosVideoReleaseLayerCallback(void *user_data,
                                 EXYNOSLayerMngEventCallbackDataPtr
                                 callback_data)
{
    XDBG_RETURN_IF_FAIL(user_data != NULL);
    XDBG_RETURN_IF_FAIL(callback_data != NULL);
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) user_data;

    XDBG_DEBUG(MVDO, "Release Callback\n");
/* Video layer try move to lowes Layer */
    if (pPort->lpos == LAYER_NONE)
        return;
    if (callback_data->release_callback.lpos < pPort->lpos
        && callback_data->release_callback.output == pPort->output) {
        {
            if (exynosLayerMngReservation(pPort->lyr_client_id,
                                          callback_data->release_callback.
                                          output,
                                          callback_data->release_callback.lpos))
                pPort->lpos = callback_data->release_callback.lpos;
        }
    }
}

/* TODO: Annex callback */
#endif

static void
_exynosVideoCvtCallback(EXYNOSCvt * cvt,
                        EXYNOSVideoBuf * src,
                        EXYNOSVideoBuf * dst, void *cvt_data, Bool error)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) cvt_data;
    DrawablePtr pDamageDrawable = NULL;
    int out_index;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    XDBG_RETURN_IF_FAIL(cvt != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(src));
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(dst));
    XDBG_DEBUG(MVDO, "++++++++++++++++++++++++ \n");
    XDBG_DEBUG(MVDO, "cvt(%p) src(%p) dst(%p)\n", cvt, src, dst);

    for (out_index = 0; out_index < OUTBUF_NUM; out_index++)
        if (pPort->outbuf[out_index] == dst)
            break;
    XDBG_RETURN_IF_FAIL(out_index < OUTBUF_NUM);

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        pDamageDrawable = pPort->pDamageDrawable[out_index];
    else
        pDamageDrawable = pPort->d.pDraw;

    XDBG_RETURN_IF_FAIL(pDamageDrawable != NULL);

    if (error) {
        DamageDamageRegion(pDamageDrawable, pPort->d.clip_boxes);
        return;
    }

    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if ((pExynos->dump_mode & XBERC_DUMP_MODE_IA) && pExynos->dump_info) {
        char file[128];
        static int i;

        snprintf(file, sizeof(file), "xvout_p%d_%03d.bmp", pPort->index, i++);
        exynosUtilDoDumpVBuf(pExynos->dump_info, dst, file);
    }

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW) {
        DamageDamageRegion(pDamageDrawable, pPort->d.clip_boxes);
    }
#ifndef LAYER_MANAGER
    else if (pPort->layer) {
        EXYNOSVideoBuf *vbuf = exynosLayerGetBuffer(pPort->layer);
        Bool reset_layer = FALSE;
        xRectangle src_rect, dst_rect;

        if (vbuf)
            if (vbuf->width != pPort->out_width ||
                vbuf->height != pPort->out_height)
                reset_layer = TRUE;

        exynosLayerGetRect(pPort->layer, &src_rect, &dst_rect);
        if (memcmp(&src_rect, &pPort->out_crop, sizeof(xRectangle)) ||
            dst_rect.x != pPort->d.dst.x ||
            dst_rect.y != pPort->d.dst.y ||
            dst_rect.width != pPort->out_crop.width ||
            dst_rect.height != pPort->out_crop.height)
            reset_layer = TRUE;

        if (reset_layer) {
            exynosLayerFreezeUpdate(pPort->layer, TRUE);

            src_rect = pPort->out_crop;
            dst_rect.x = pPort->d.dst.x;
            dst_rect.y = pPort->d.dst.y;
            dst_rect.width = pPort->out_crop.width;
            dst_rect.height = pPort->out_crop.height;

            exynosLayerSetRect(pPort->layer, &src_rect, &dst_rect);
            exynosLayerFreezeUpdate(pPort->layer, FALSE);
            exynosLayerSetBuffer(pPort->layer, dst);
        }
        else
            exynosLayerSetBuffer(pPort->layer, dst);

        if (!exynosLayerIsVisible(pPort->layer) && !pExynos->XVHIDE)
            exynosLayerShow(pPort->layer);
        else if (pExynos->XVHIDE)
            exynosLayerHide(pPort->layer);
    }
#else
    else if (pPort->lpos != LAYER_NONE) {
        xRectangle src_rect, dst_rect;

        src_rect = pPort->out_crop;
        dst_rect.x = pPort->d.dst.x;
        dst_rect.y = pPort->d.dst.y;
        dst_rect.width = pPort->out_crop.width;
        dst_rect.height = pPort->out_crop.height;
        EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

        XDBG_RETURN_IF_FAIL(pVideo != NULL);
        if (pPort->lpos != pPort->old_lpos) {
            exynosLayerMngSet(pPort->lyr_client_id, pVideo->video_offset_x,
                              pVideo->video_offset_y, &src_rect, &dst_rect,
                              NULL, dst, pPort->output, pPort->old_lpos, NULL,
                              NULL);
        }
        else {
            exynosLayerMngSet(pPort->lyr_client_id, pVideo->video_offset_x,
                              pVideo->video_offset_y, &src_rect, &dst_rect,
                              NULL, dst, pPort->output, pPort->lpos, NULL,
                              NULL);
        }
    }
#endif
    XDBG_DEBUG(MVDO, "++++++++++++++++++++++++.. \n");
}

static void
_exynosVideoTvoutCvtCallback(EXYNOSCvt * cvt,
                             EXYNOSVideoBuf * src,
                             EXYNOSVideoBuf * dst, void *cvt_data, Bool error)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) cvt_data;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    XDBG_RETURN_IF_FAIL(cvt != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(src));
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(dst));

    XDBG_DEBUG(MVDO, "######################## \n");
    XDBG_DEBUG(MVDO, "cvt(%p) src(%p) dst(%p)\n", cvt, src, dst);

    if (pPort->wait_vbuf != src)
        XDBG_WARNING(MVDO, "wait_vbuf(%p) != src(%p). \n",
                     pPort->wait_vbuf, src);

    pPort->wait_vbuf = NULL;

    XDBG_DEBUG(MVDO, "########################.. \n");
}

static void
_exynosVideoLayerNotifyFunc(EXYNOSLayer * layer, int type, void *type_data,
                            void *data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    EXYNOSVideoBuf *vbuf = (EXYNOSVideoBuf *) type_data;

    if (type != LAYER_VBLANK)
        return;

    XDBG_RETURN_IF_FAIL(pPort != NULL);
    XDBG_RETURN_IF_FAIL(VBUF_IS_VALID(vbuf));

    if (pPort->wait_vbuf != vbuf)
        XDBG_WARNING(MVDO, "wait_vbuf(%p) != vbuf(%p). \n",
                     pPort->wait_vbuf, vbuf);

    XDBG_DEBUG(MVBUF, "now_showing(%p). \n", vbuf);

    pPort->wait_vbuf = NULL;
}

static void
_exynosVideoEnsureConverter(EXYNOSPortPrivPtr pPort)
{
    if (pPort->cvt)
        return;

    pPort->cvt = exynosCvtCreate(pPort->pScrn, CVT_OP_M2M);
    XDBG_RETURN_IF_FAIL(pPort->cvt != NULL);

    exynosCvtAddCallback(pPort->cvt, _exynosVideoCvtCallback, pPort);
}

static void
_exynosVideoCloseConverter(EXYNOSPortPrivPtr pPort)
{
    if (pPort->cvt) {
        exynosCvtDestroy(pPort->cvt);
        pPort->cvt = NULL;
    }

    XDBG_TRACE(MVDO, "done. \n");
}

static void
_exynosVideoStreamOff(EXYNOSPortPrivPtr pPort)
{
    _exynosVideoCloseConverter(pPort);
    _exynosVideoUngrabTvout(pPort);
    _exynosVideoCloseInBuffer(pPort);
    _exynosVideoCloseOutBuffer(pPort, TRUE);

    EXYNOSWb *wb = exynosWbGet();

    if (wb) {
        if (pPort->need_start_wb) {
            exynosWbSetSecure(wb, FALSE);
            exynosWbStart(wb);
            pPort->need_start_wb = FALSE;
        }
        else
            exynosWbSetSecure(wb, FALSE);
    }

    if (pPort->d.clip_boxes) {
        RegionDestroy(pPort->d.clip_boxes);
        pPort->d.clip_boxes = NULL;
    }

    memset(&pPort->old_d, 0, sizeof(PutData));
    memset(&pPort->d, 0, sizeof(PutData));

    pPort->need_start_wb = FALSE;
    pPort->skip_tvout = FALSE;
    pPort->usr_output = OUTPUT_LCD | OUTPUT_EXT | OUTPUT_FULL;
    pPort->outbuf_cvting = -1;
    pPort->drawing = 0;
    pPort->tv_prev_time = 0;
    pPort->exynosure = FALSE;
    pPort->csc_range = 0;
    pPort->inbuf_is_fb = FALSE;

    if (pPort->stream_cnt > 0) {
        pPort->stream_cnt = 0;
        XDBG_SECURE(MVDO, "pPort(%d) stream off. \n", pPort->index);

        if (pPort->preemption > -1)
            streaming_ports--;

        XDBG_WARNING_IF_FAIL(streaming_ports >= 0);
    }
#ifdef LAYER_MANAGER
    if (pPort->lyr_client_id != LYR_ERROR_ID) {
        exynosLayerMngUnRegisterClient(pPort->lyr_client_id);
        pPort->lyr_client_id = LYR_ERROR_ID;
        pPort->lpos = LAYER_NONE;
    }
#endif
    XDBG_TRACE(MVDO, "done. \n");
}

static Bool
_exynosVideoCalculateSize(EXYNOSPortPrivPtr pPort)
{
    EXYNOSCvtProp src_prop = { 0, }, dst_prop = {
    0,};

    src_prop.id = pPort->d.id;
    src_prop.width = pPort->d.width;
    src_prop.height = pPort->d.height;
    src_prop.crop = pPort->d.src;

    dst_prop.id = FOURCC_RGB32;
    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW) {
        dst_prop.width = pPort->d.pDraw->width;
        dst_prop.height = pPort->d.pDraw->height;
        dst_prop.crop = pPort->d.dst;
        dst_prop.crop.x -= pPort->d.pDraw->x;
        dst_prop.crop.y -= pPort->d.pDraw->y;
    }
    else {
        dst_prop.width = pPort->d.dst.width;
        dst_prop.height = pPort->d.dst.height;
        dst_prop.crop = pPort->d.dst;
        dst_prop.crop.x = 0;
        dst_prop.crop.y = 0;
    }

    XDBG_DEBUG(MVDO, "(%dx%d : %d,%d %dx%d) => (%dx%d : %d,%d %dx%d)\n",
               src_prop.width, src_prop.height,
               src_prop.crop.x, src_prop.crop.y, src_prop.crop.width,
               src_prop.crop.height, dst_prop.width, dst_prop.height,
               dst_prop.crop.x, dst_prop.crop.y, dst_prop.crop.width,
               dst_prop.crop.height);

    if (!exynosCvtEnsureSize(&src_prop, &dst_prop))
        return FALSE;

    XDBG_DEBUG(MVDO, "(%dx%d : %d,%d %dx%d) => (%dx%d : %d,%d %dx%d)\n",
               src_prop.width, src_prop.height,
               src_prop.crop.x, src_prop.crop.y, src_prop.crop.width,
               src_prop.crop.height, dst_prop.width, dst_prop.height,
               dst_prop.crop.x, dst_prop.crop.y, dst_prop.crop.width,
               dst_prop.crop.height);

    XDBG_RETURN_VAL_IF_FAIL(src_prop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src_prop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src_prop.crop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(src_prop.crop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_prop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_prop.height > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_prop.crop.width > 0, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(dst_prop.crop.height > 0, FALSE);

    pPort->in_width = src_prop.width;
    pPort->in_height = src_prop.height;
    pPort->in_crop = src_prop.crop;

    pPort->out_width = dst_prop.width;
    pPort->out_height = dst_prop.height;
    pPort->out_crop = dst_prop.crop;
    return TRUE;
}

static void
_exynosVideoPunchDrawable(EXYNOSPortPrivPtr pPort)
{
    PixmapPtr pPixmap = _getPixmap(pPort->d.pDraw);
    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if (pPort->drawing != ON_FB || !pExynos->pVideoPriv->video_punch)
        return;

    if (!pPort->punched) {
        exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
        if (pPixmap->devPrivate.ptr)
            memset(pPixmap->devPrivate.ptr, 0,
                   pPixmap->drawable.width * pPixmap->drawable.height * 4);
        exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);
        XDBG_TRACE(MVDO, "Punched (%dx%d) %p. \n",
                   pPixmap->drawable.width, pPixmap->drawable.height,
                   pPixmap->devPrivate.ptr);
        pPort->punched = TRUE;
        DamageDamageRegion(pPort->d.pDraw, pPort->d.clip_boxes);
    }
}

static Bool
_exynosVideoSupportID(int id)
{
    int i;

    for (i = 0; i < NUM_IMAGES; i++)
        if (images[i].id == id)
            if (exynosCvtSupportFormat(CVT_OP_M2M, id))
                return TRUE;

    return FALSE;
}

static Bool
_exynosVideoInBranch(WindowPtr p, WindowPtr w)
{
    for (; w; w = w->parent)
        if (w == p)
            return TRUE;

    return FALSE;
}

/* Return the child of 'p' which includes 'w'. */
static WindowPtr
_exynosVideoGetChild(WindowPtr p, WindowPtr w)
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
_exynosVideoGetAncestors(WindowPtr a, WindowPtr b,
                         WindowPtr *ancestor,
                         WindowPtr *ancestor_a, WindowPtr *ancestor_b)
{
    WindowPtr child_a, child_b;

    if (!ancestor || !ancestor_a || !ancestor_b)
        return FALSE;

    for (child_b = b, b = b->parent; b; child_b = b, b = b->parent) {
        child_a = _exynosVideoGetChild(b, a);
        if (child_a) {
            *ancestor = b;
            *ancestor_a = child_a;
            *ancestor_b = child_b;
            return TRUE;
        }
    }

    return FALSE;
}

static int
_exynosVideoCompareWindow(WindowPtr pWin1, WindowPtr pWin2)
{
    WindowPtr a, a1, a2, c;

    if (!pWin1 || !pWin2)
        return -2;

    if (pWin1 == pWin2)
        return 0;

    if (_exynosVideoGetChild(pWin1, pWin2))
        return -1;

    if (_exynosVideoGetChild(pWin2, pWin1))
        return 1;

    if (!_exynosVideoGetAncestors(pWin1, pWin2, &a, &a1, &a2))
        return -3;

    for (c = a->firstChild; c; c = c->nextSib) {
        if (c == a1)
            return 1;
        else if (c == a2)
            return -1;
    }

    return -4;
}

static void
_exynosVideoArrangeLayerPos(EXYNOSPortPrivPtr pPort, Bool by_notify)
{
    EXYNOSPortPrivPtr pCur = NULL, pNext = NULL;
    EXYNOSPortPrivPtr pAnother = NULL;
    int i = 0;

    xorg_list_for_each_entry_safe(pCur, pNext, &layer_owners, link) {
        if (pCur == pPort)
            continue;

        i++;

        if (!pAnother)
            pAnother = pCur;
        else
            XDBG_WARNING(MVDO, "There are 3 more V4L2 ports. (%d) \n", i);
    }
#ifndef LAYER_MANAGER
    if (!pAnother) {
        EXYNOSLayerPos lpos = exynosLayerGetPos(pPort->layer);

        if (lpos == LAYER_NONE)
            exynosLayerSetPos(pPort->layer, LAYER_LOWER2);
    }
    else {
        EXYNOSLayerPos lpos1 = LAYER_NONE;
        EXYNOSLayerPos lpos2 = LAYER_NONE;

        if (pAnother->layer)
            lpos1 = exynosLayerGetPos(pAnother->layer);
        if (pPort->layer)
            lpos2 = exynosLayerGetPos(pPort->layer);

        if (lpos2 == LAYER_NONE) {
            int comp = _exynosVideoCompareWindow((WindowPtr) pAnother->d.pDraw,
                                                 (WindowPtr) pPort->d.pDraw);

            XDBG_TRACE(MVDO, "0x%08x : 0x%08x => %d \n",
                       _XID(pAnother->d.pDraw), _XID(pPort->d.pDraw), comp);

            if (comp == 1) {
                if (lpos1 != LAYER_LOWER1) {
                    exynosLayerSetPos(pAnother->layer, LAYER_LOWER1);
                }
                exynosLayerSetPos(pPort->layer, LAYER_LOWER2);
            }
            else if (comp == -1) {
                if (lpos1 != LAYER_LOWER2)
                    exynosLayerSetPos(pAnother->layer, LAYER_LOWER2);
                exynosLayerSetPos(pPort->layer, LAYER_LOWER1);
            }
            else {
                if (lpos1 == LAYER_LOWER1)
                    exynosLayerSetPos(pPort->layer, LAYER_LOWER2);
                else
                    exynosLayerSetPos(pPort->layer, LAYER_LOWER1);
            }
        }
        else {
            if (!by_notify)
                return;

            int comp = _exynosVideoCompareWindow((WindowPtr) pAnother->d.pDraw,
                                                 (WindowPtr) pPort->d.pDraw);

            XDBG_TRACE(MVDO, "0x%08x : 0x%08x => %d \n",
                       _XID(pAnother->d.pDraw), _XID(pPort->d.pDraw), comp);

            if ((comp == 1 && lpos1 != LAYER_LOWER1) ||
                (comp == -1 && lpos2 != LAYER_LOWER1))
                exynosLayerSwapPos(pAnother->layer, pPort->layer);
        }
    }
#else
    if (!by_notify || !pAnother)
        return;
    int comp = _exynosVideoCompareWindow((WindowPtr) pAnother->d.pDraw,
                                         (WindowPtr) pPort->d.pDraw);

    XDBG_TRACE(MVDO, "0x%08x : 0x%08x => %d \n",
               _XID(pAnother->d.pDraw), _XID(pPort->d.pDraw), comp);
    if ((comp == 1 && pAnother->lpos != LAYER_LOWER1) ||
        (comp == -1 && pPort->lpos != LAYER_LOWER1)) {
        if (exynosLayerMngSwapPos(pAnother->lyr_client_id,
                                  pAnother->output, pAnother->lpos,
                                  pPort->lyr_client_id,
                                  pPort->output, pPort->lpos)) {
            EXYNOSLayerPos temp_lpos;

            temp_lpos = pAnother->lpos;
            pAnother->lpos = pPort->lpos;
            pPort->lpos = temp_lpos;
        }
    }
#endif
}

static void
_exynosVideoStopTvout(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pExynos->pVideoPriv->pAdaptor[0];
    int i;

    for (i = 0; i < EXYNOS_MAX_PORT; i++) {
        EXYNOSPortPrivPtr pPort =
            (EXYNOSPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->grab_tvout) {
            _exynosVideoUngrabTvout(pPort);
            return;
        }
    }
}

/* TRUE  : current frame will be shown on TV. free after vblank.
 * FALSE : current frame won't be shown on TV.
 */
static Bool
_exynosVideoPutImageTvout(EXYNOSPortPrivPtr pPort, int output,
                          EXYNOSVideoBuf * inbuf)
{
    ScrnInfoPtr pScrn = pPort->pScrn;
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    xRectangle tv_rect = { 0, };
    Bool first_put = FALSE;

    if (!(output & OUTPUT_EXT)) {
        XDBG_DEBUG(MTVO, "!(output (%d) & OUTPUT_EXT)\n", output);
        return FALSE;
    }

    if (pPort->skip_tvout) {
        XDBG_DEBUG(MTVO, "pPort->skip_tvout (%d)\n", pPort->skip_tvout);
        return FALSE;
    }

    if (!_exynosVideoGrabTvout(pPort))
        goto fail_to_put_tvout;

    if (!pPort->tv) {
        EXYNOSCvt *tv_cvt;
        EXYNOSWb *wb;

        if (!exynosUtilEnsureExternalCrtc(pScrn)) {
            XDBG_ERROR(MVDO, "failed : pPort(%d) connect external crtc\n",
                       pPort->index);
            goto fail_to_put_tvout;
        }

        pPort->tv = exynosVideoTvConnect(pScrn, pPort->d.id, LAYER_LOWER1);
        XDBG_GOTO_IF_FAIL(pPort->tv != NULL, fail_to_put_tvout);
        exynosVideoTvSetAttributes(pPort->tv, pPort->hw_rotate, pPort->hflip,
                                   pPort->vflip);
        wb = exynosWbGet();
        if (wb) {
            pPort->need_start_wb = TRUE;
#if 0
            /* in case of VIRTUAL, wb's buffer is used by tvout. */
            if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL)
                exynosWbStop(wb, FALSE);
            else
                exynosWbStop(wb, TRUE);
#endif
        }
#if 0
        if (exynosWbIsRunning()) {
            XDBG_ERROR(MVDO, "failed: wb still running\n");
            goto fail_to_put_tvout;
        }
#endif
        if (!exynosVideoTvGetConverter(pPort->tv)) {
            if (!exynosVideoTvCanDirectDrawing
                (pPort->tv, pPort->d.src.width, pPort->d.src.height,
                 pPort->d.dst.width, pPort->d.dst.height)) {
                XDBG_GOTO_IF_FAIL(exynosVideoTvReCreateConverter(pPort->tv),
                                  fail_to_put_tvout);
            }
        }

        tv_cvt = exynosVideoTvGetConverter(pPort->tv);

        if (tv_cvt) {
            /* HDMI    : SN12
             * VIRTUAL : SN12 or RGB32
             */
#if 0
            if (pExynosMode->conn_mode == DISPLAY_CONN_MODE_VIRTUAL) {
                if (pExynosMode->set_mode == DISPLAY_SET_MODE_CLONE) {
                    EXYNOSVideoBuf **vbufs = NULL;
                    int bufnum = 0;

                    exynosVideoTvSetConvertFormat(pPort->tv, FOURCC_SN12);

                    /* In case of virtual, we draw video on full-size buffer
                     * for virtual-adaptor
                     */
                    exynosVideoTvSetSize(pPort->tv,
                                         pExynosMode->ext_connector_mode.
                                         hdisplay,
                                         pExynosMode->ext_connector_mode.
                                         vdisplay);

                    exynosVirtualVideoGetBuffers(pPort->pScrn, FOURCC_SN12,
                                                 pExynosMode->ext_connector_mode.
                                                 hdisplay,
                                                 pExynosMode->ext_connector_mode.
                                                 vdisplay, &vbufs, &bufnum);

                    XDBG_GOTO_IF_FAIL(vbufs != NULL, fail_to_put_tvout);
                    XDBG_GOTO_IF_FAIL(bufnum > 0, fail_to_put_tvout);

                    exynosVideoTvSetBuffer(pPort->tv, vbufs, bufnum);
                }
                else            /* desktop */
                    exynosVideoTvSetConvertFormat(pPort->tv, FOURCC_RGB32);
            }

            else {
#if 0
                exynosVideoTvSetConvertFormat(pPort->tv, FOURCC_SN12);
#endif
            }
#endif
            exynosCvtAddCallback(tv_cvt, _exynosVideoTvoutCvtCallback, pPort);
        }
        else {
            EXYNOSLayer *layer = exynosVideoTvGetLayer(pPort->tv);

            XDBG_GOTO_IF_FAIL(layer != NULL, fail_to_put_tvout);
            exynosLayerEnableVBlank(layer, TRUE);
            exynosLayerAddNotifyFunc(layer, _exynosVideoLayerNotifyFunc, pPort);
        }

        first_put = TRUE;
    }

    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if (pPort->wait_vbuf) {
        if (pExynos->pVideoPriv->video_fps) {
            CARD32 cur, sub;

            cur = GetTimeInMillis();
            sub = cur - pPort->tv_prev_time;
            pPort->tv_prev_time = cur;

            XDBG_DEBUG(MVDO, "tvout skip : sub(%ld) vbuf(%ld:%d,%d,%d) \n",
                       sub, inbuf->stamp,
                       inbuf->keys[0], inbuf->keys[1], inbuf->keys[2]);
        }
        XDBG_DEBUG(MVDO, "pPort->wait_vbuf (%p) skip_frame\n",
                   pPort->wait_vbuf);
        return FALSE;
    }
    else if (pExynos->pVideoPriv->video_fps)
        pPort->tv_prev_time = GetTimeInMillis();

    if (!(output & OUTPUT_FULL)) {
        EXYNOSDisplaySetMode disp_mode = exynosDisplayGetDispSetMode(pScrn);

        if (disp_mode == DISPLAY_SET_MODE_EXT)
            tv_rect.x = pPort->d.dst.x - pExynosMode->main_lcd_mode.hdisplay;
        else
            tv_rect.x = pPort->d.dst.x;
        tv_rect.y = pPort->d.dst.y;
        tv_rect.width = pPort->d.dst.width;
        tv_rect.height = pPort->d.dst.height;
    }
    else {
        exynosUtilAlignRect(pPort->d.src.width, pPort->d.src.height,
                            pExynosMode->ext_connector_mode.hdisplay,
                            pExynosMode->ext_connector_mode.vdisplay, &tv_rect,
                            TRUE);
    }

    /* if exynosVideoTvPutImage returns FALSE, it means this frame won't show on TV. */
    if (!exynosVideoTvPutImage(pPort->tv, inbuf, &tv_rect, pPort->csc_range))
        return FALSE;

    if (first_put && !(output & OUTPUT_LCD))
        _exynosVideoSetOutputExternalProperty(pPort->d.pDraw, TRUE);

    pPort->wait_vbuf = inbuf;

    return TRUE;

 fail_to_put_tvout:
    _exynosVideoUngrabTvout(pPort);

    pPort->skip_tvout = TRUE;

    XDBG_TRACE(MVDO, "pPort(%d) skip tvout \n", pPort->index);

    return FALSE;
}

static Bool
_exynosVideoPutImageInbuf(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * inbuf)
{
#ifndef LAYER_MANAGER
    EXYNOSPtr pExynos = EXYNOSPTR(pPort->pScrn);

    if (!pPort->layer) {
        pPort->layer = _exynosVideoCreateLayer(pPort);
        XDBG_RETURN_VAL_IF_FAIL(pPort->layer != NULL, FALSE);

        _exynosVideoArrangeLayerPos(pPort, FALSE);
    }
    exynosLayerSetBuffer(pPort->layer, inbuf);

    if (!exynosLayerIsVisible(pPort->layer) && !pExynos->XVHIDE)
        exynosLayerShow(pPort->layer);
    else if (pExynos->XVHIDE)
        exynosLayerHide(pPort->layer);
#else
    if (pPort->lpos == LAYER_NONE) {
        XDBG_RETURN_VAL_IF_FAIL(_exynosVideoCreateLayer(pPort), FALSE);
    }
    else if (pPort->lpos != pPort->old_lpos) {
        exynosLayerMngRelease(pPort->lyr_client_id, pPort->output,
                              pPort->old_lpos);
    }
    xRectangle src, dst;

    src = dst = pPort->out_crop;
    dst.x = pPort->d.dst.x;
    dst.y = pPort->d.dst.y;
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pPort->pScrn)->pVideoPriv;

    XDBG_RETURN_VAL_IF_FAIL(pVideo != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(exynosLayerMngSet(pPort->lyr_client_id,
                                              pVideo->video_offset_x,
                                              pVideo->video_offset_y, &src,
                                              &dst, NULL, inbuf, pPort->output,
                                              pPort->lpos, NULL, NULL)
                            , FALSE);
#endif
    return TRUE;
}

static Bool
_exynosVideoPutImageInternal(EXYNOSPortPrivPtr pPort, EXYNOSVideoBuf * inbuf)
{
    EXYNOSPtr pExynos = (EXYNOSPtr) pPort->pScrn->driverPrivate;
    EXYNOSCvtProp src_prop = { 0, }, dst_prop = {
    0,};
    EXYNOSVideoBuf *outbuf = NULL;

    outbuf = _exynosVideoGetOutbuf(pPort);
    if (!outbuf)
        return FALSE;

    /* cacheflush here becasue dst buffer can be created in _exynosVideoGetOutbuf() */
    if (pPort->stream_cnt == 1)
        if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
            exynosUtilCacheFlush(pPort->pScrn);

    XDBG_DEBUG(MVDO, "'%c%c%c%c' preem(%d) rot(%d) \n",
               FOURCC_STR(pPort->d.id), pPort->preemption, pPort->hw_rotate);
#ifndef LAYER_MANAGER
    if (pPort->layer)
        _exynosVideoArrangeLayerPos(pPort, FALSE);
#endif
    _exynosVideoEnsureConverter(pPort);
    XDBG_GOTO_IF_FAIL(pPort->cvt != NULL, fail_to_put);

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
    dst_prop.exynosure = pPort->exynosure;
    dst_prop.csc_range = pPort->csc_range;

    if (!exynosCvtEnsureSize(&src_prop, &dst_prop))
        goto fail_to_put;

    if (!exynosCvtSetProperpty(pPort->cvt, &src_prop, &dst_prop))
        goto fail_to_put;

    if (!exynosCvtConvert(pPort->cvt, inbuf, outbuf))
        goto fail_to_put;

    if (pExynos->pVideoPriv->video_fps)
        _countFps(pPort);

    exynosUtilVideoBufferUnref(outbuf);

    return TRUE;

 fail_to_put:
    if (outbuf)
        exynosUtilVideoBufferUnref(outbuf);

    _exynosVideoCloseConverter(pPort);
    _exynosVideoCloseOutBuffer(pPort, TRUE);

    return FALSE;
}

static Bool
_exynosVideoSetHWPortsProperty(ScreenPtr pScreen, int nums)
{
    WindowPtr pWin = pScreen->root;
    Atom atom_hw_ports;

    /* With "X_HW_PORTS", an application can know
     * how many fimc devices XV uses.
     */
    if (!pWin || !serverClient)
        return FALSE;

    atom_hw_ports = MakeAtom("XV_HW_PORTS", strlen("XV_HW_PORTS"), TRUE);

    dixChangeWindowProperty(serverClient,
                            pWin, atom_hw_ports, XA_CARDINAL, 32,
                            PropModeReplace, 1, (unsigned int *) &nums, FALSE);

    return TRUE;
}

static Bool
_exynosVideoSetOutputExternalProperty(DrawablePtr pDraw, Bool video_only)
{
    WindowPtr pWin;
    Atom atom_external;

    XDBG_RETURN_VAL_IF_FAIL(pDraw != NULL, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(pDraw->type == DRAWABLE_WINDOW, FALSE);

    pWin = (WindowPtr) pDraw;

    atom_external =
        MakeAtom("XV_OUTPUT_EXTERNAL", strlen("XV_OUTPUT_EXTERNAL"), TRUE);

    dixChangeWindowProperty(clients[CLIENT_ID(pDraw->id)],
                            pWin, atom_external, XA_CARDINAL, 32,
                            PropModeReplace, 1, (unsigned int *) &video_only,
                            TRUE);

    XDBG_TRACE(MVDO, "pDraw(0x%lx) video-only(%s)\n",
               pDraw->id, (video_only) ? "ON" : "OFF");

    return TRUE;
}

static void
_exynosVideoRestackWindow(WindowPtr pWin, WindowPtr pOldNextSib)
{
    ScreenPtr pScreen = ((DrawablePtr) pWin)->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    EXYNOSVideoPrivPtr pVideo = pExynos->pVideoPriv;

    if (pVideo->RestackWindow) {
        pScreen->RestackWindow = pVideo->RestackWindow;

        if (pScreen->RestackWindow)
            (*pScreen->RestackWindow) (pWin, pOldNextSib);

        pVideo->RestackWindow = pScreen->RestackWindow;
        pScreen->RestackWindow = _exynosVideoRestackWindow;
    }

    if (!xorg_list_is_empty(&layer_owners)) {
        EXYNOSPortPrivPtr pCur = NULL, pNext = NULL;

        xorg_list_for_each_entry_safe(pCur, pNext, &layer_owners, link) {
            if (_exynosVideoInBranch(pWin, (WindowPtr) pCur->d.pDraw)) {
                XDBG_TRACE(MVDO, "Do re-arrange. 0x%08x(0x%08x) \n",
                           _XID(pWin), _XID(pCur->d.pDraw));
                _exynosVideoArrangeLayerPos(pCur, TRUE);
                break;
            }
        }
    }
}

static void
_exynosVideoBlockHandler(pointer data, OSTimePtr pTimeout, pointer pRead)
{
    ScreenPtr pScreen = ((ScrnInfoPtr) data)->pScreen;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    EXYNOSVideoPrivPtr pVideo = pExynos->pVideoPriv;

    pVideo->RestackWindow = pScreen->RestackWindow;
    pScreen->RestackWindow = _exynosVideoRestackWindow;

    if (registered_handler &&
        _exynosVideoSetHWPortsProperty(pScreen, NUM_HW_LAYER)) {
        RemoveBlockAndWakeupHandlers(_exynosVideoBlockHandler,
                                     (WakeupHandlerProcPtr) NoopDDA, data);
        registered_handler = FALSE;
    }
}

static Bool
_exynosVideoAddDrawableEvent(EXYNOSPortPrivPtr pPort)
{
    EXYNOSVideoResource *resource;
    void *ptr = NULL;
    int ret;

    ret = dixLookupResourceByType(&ptr, pPort->d.pDraw->id,
                                  event_drawable_type, NULL, DixWriteAccess);
    if (ret == Success) {
        return TRUE;
    }

    resource = malloc(sizeof(EXYNOSVideoResource));
    if (resource == NULL)
        return FALSE;

    if (!AddResource(pPort->d.pDraw->id, event_drawable_type, resource)) {
        free(resource);
        return FALSE;
    }

    XDBG_TRACE(MVDO, "id(0x%08lx). \n", pPort->d.pDraw->id);

    resource->id = pPort->d.pDraw->id;
    resource->type = event_drawable_type;
    resource->pPort = pPort;
    resource->pScrn = pPort->pScrn;

    return TRUE;
}

static int
_exynosVideoRegisterEventDrawableGone(void *data, XID id)
{
    EXYNOSVideoResource *resource = (EXYNOSVideoResource *) data;

    XDBG_TRACE(MVDO, "id(0x%08lx). \n", id);

    if (!resource)
        return Success;

    if (!resource->pPort || !resource->pScrn)
        return Success;

    EXYNOSVideoStop(resource->pScrn, (pointer) resource->pPort, 1);

    free(resource);

    return Success;
}

static Bool
_exynosVideoRegisterEventResourceTypes(void)
{
    event_drawable_type =
        CreateNewResourceType(_exynosVideoRegisterEventDrawableGone,
                              "Sec Video Drawable");

    if (!event_drawable_type)
        return FALSE;

    return TRUE;
}

static int
_exynosVideoCheckChange(EXYNOSPortPrivPtr pPort)
{
    int ret = CHANGED_NONE;

    if (pPort->d.id != pPort->old_d.id
        || pPort->d.width != pPort->old_d.width
        || pPort->d.height != pPort->old_d.height
        || memcmp(&pPort->d.src, &pPort->old_d.src, sizeof(xRectangle))
        || pPort->old_secure != pPort->exynosure
        || pPort->old_csc_range != pPort->csc_range) {
        XDBG_DEBUG(MVDO,
                   "pPort(%d) old_src(%dx%d %d,%d %dx%d) : new_src(%dx%d %d,%d %dx%d)\n",
                   pPort->index, pPort->old_d.width, pPort->old_d.height,
                   pPort->old_d.src.x, pPort->old_d.src.y,
                   pPort->old_d.src.width, pPort->old_d.src.height,
                   pPort->d.width, pPort->d.height, pPort->d.src.x,
                   pPort->d.src.y, pPort->d.src.width, pPort->d.src.height);
        XDBG_DEBUG(MVDO,
                   "old_secure(%d) old_csc(%d) : new_secure(%d) new_csc(%d)\n",
                   pPort->old_secure, pPort->old_csc_range, pPort->exynosure,
                   pPort->csc_range);

        ret += CHANGED_INPUT;
    }

    if (memcmp(&pPort->d.dst, &pPort->old_d.dst, sizeof(xRectangle))
        || pPort->old_rotate != pPort->rotate
        || pPort->old_hflip != pPort->hflip
        || pPort->old_vflip != pPort->vflip) {
        XDBG_DEBUG(MVDO,
                   "pPort(%d) old_dst(%d,%d %dx%d) : new_dst(%dx%d %dx%d)\n",
                   pPort->index, pPort->old_d.dst.x, pPort->old_d.dst.y,
                   pPort->old_d.dst.width, pPort->old_d.dst.height,
                   pPort->d.dst.x, pPort->d.dst.y, pPort->d.dst.width,
                   pPort->d.dst.height);
        XDBG_DEBUG(MVDO, "old_rotate(%d) old_hflip(%d) old_vflip(%d)\n",
                   pPort->old_rotate, pPort->old_hflip, pPort->old_vflip);
        XDBG_DEBUG(MVDO, "new_rotate (%d) new_hflip(%d) new_vflip(%d)\n",
                   pPort->rotate, pPort->hflip, pPort->vflip);
        ret += CHANGED_OUTPUT;  // output changed
    }

    return ret;
}

int
exynosVideoQueryImageAttrs(ScrnInfoPtr pScrn,
                           int id,
                           int *w,
                           int *h, int *pitches, int *offsets, int *lengths)
{
    int size = 0, tmp = 0;

    *w = (*w + 1) & ~1;
    if (offsets)
        offsets[0] = 0;

    switch (id) {
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
            pitches[1] = *w;

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
            pitches[1] = *w;

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
EXYNOSVideoGetPortAttribute(ScrnInfoPtr pScrn,
                            Atom attribute, INT32 *value, pointer data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (attribute == _portAtom(PAA_ROTATION)) {
        *value = pPort->rotate;
        return Success;
    }
    else if (attribute == _portAtom(PAA_HFLIP)) {
        *value = pPort->hflip;
        return Success;
    }
    else if (attribute == _portAtom(PAA_VFLIP)) {
        *value = pPort->vflip;
        return Success;
    }
    else if (attribute == _portAtom(PAA_PREEMPTION)) {
        *value = pPort->preemption;
        return Success;
    }
    else if (attribute == _portAtom(PAA_OUTPUT)) {
        *value = pPort->usr_output;
        return Success;
    }
    else if (attribute == _portAtom(PAA_SECURE)) {
        *value = pPort->exynosure;
        return Success;
    }
    else if (attribute == _portAtom(PAA_CSC_RANGE)) {
        *value = pPort->csc_range;
        return Success;
    }

    return BadMatch;
}

static int
EXYNOSVideoSetPortAttribute(ScrnInfoPtr pScrn,
                            Atom attribute, INT32 value, pointer data)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (attribute == _portAtom(PAA_ROTATION)) {
        pPort->rotate = value;
        XDBG_DEBUG(MVDO, "rotate(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _portAtom(PAA_HFLIP)) {
        pPort->hflip = value;
        XDBG_DEBUG(MVDO, "hflip(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _portAtom(PAA_VFLIP)) {
        pPort->vflip = value;
        XDBG_DEBUG(MVDO, "vflip(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _portAtom(PAA_PREEMPTION)) {
        pPort->preemption = value;
        XDBG_DEBUG(MVDO, "preemption(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _portAtom(PAA_OUTPUT)) {
        pPort->usr_output = (OUTPUT_LCD | OUTPUT_EXT | OUTPUT_FULL) & value;

        XDBG_DEBUG(MVDO, "output (%d) \n", (int) value);

        return Success;
    }
    else if (attribute == _portAtom(PAA_SECURE)) {
        pPort->exynosure = value;
        XDBG_DEBUG(MVDO, "exynosure(%d) \n", (int) value);
        return Success;
    }
    else if (attribute == _portAtom(PAA_CSC_RANGE)) {
        pPort->csc_range = value;
        XDBG_DEBUG(MVDO, "csc_range(%d) \n", (int) value);
        return Success;
    }

    return Success;
}

static void
EXYNOSVideoQueryBestSize(ScrnInfoPtr pScrn,
                         Bool motion,
                         short vid_w, short vid_h,
                         short dst_w, short dst_h,
                         uint * p_w, uint * p_h, pointer data)
{
    EXYNOSCvtProp prop = { 0, };

    if (!p_w && !p_h)
        return;

    prop.width = dst_w;
    prop.height = dst_h;
    prop.crop.width = dst_w;
    prop.crop.height = dst_h;

    if (exynosCvtEnsureSize(NULL, &prop)) {
        if (p_w)
            *p_w = prop.width;
        if (p_h)
            *p_h = prop.height;
    }
    else {
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
EXYNOSVideoQueryImageAttributes(ScrnInfoPtr pScrn,
                                int id,
                                unsigned short *w,
                                unsigned short *h, int *pitches, int *offsets)
{
    int width, height, size;

    if (!w || !h)
        return 0;

    width = (int) *w;
    height = (int) *h;

    size =
        exynosVideoQueryImageAttrs(pScrn, id, &width, &height, pitches, offsets,
                                   NULL);

    *w = (unsigned short) width;
    *h = (unsigned short) height;

    return size;
}

/* coordinates : HW, SCREEN, PORT
 * BadRequest : when video can't be shown or drawn.
 * Success    : A damage event(pixmap) and inbuf should be return.
 *              If can't return a damage event and inbuf, should be return
 *              BadRequest.
 */
static int
EXYNOSVideoPutImage(ScrnInfoPtr pScrn,
                    short src_x, short src_y, short dst_x, short dst_y,
                    short src_w, short src_h, short dst_w, short dst_h,
                    int id, uchar * buf, short width, short height,
                    Bool sync, RegionPtr clip_boxes, pointer data,
                    DrawablePtr pDraw)
{
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

#ifdef NO_CRTC_MODE
    if (pExynos->isCrtcOn == FALSE) {
        XDBG_WARNING(MVDO, "XV PutImage Disabled (No active CRTCs)\n");
        return BadRequest;
    }
#endif
    EXYNOSModePtr pExynosMode = (EXYNOSModePtr) EXYNOSPTR(pScrn)->pExynosMode;
    EXYNOSVideoPrivPtr pVideo = EXYNOSPTR(pScrn)->pVideoPriv;
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;
    int output, ret;
    Bool tvout = FALSE, lcdout = FALSE;
    EXYNOSVideoBuf *inbuf = NULL;
    int old_drawing;

#ifdef LAYER_MANAGER
    if (pPort->lyr_client_id == LYR_ERROR_ID) {
        char client_name[20] = { 0 };
        if (snprintf
            (client_name, sizeof(client_name), "XV_PUTIMAGE-%d",
             pPort->index) < 0) {
            XDBG_ERROR(MVDO, "Can't register layer manager client\n");
            return BadRequest;
        }
        pPort->lyr_client_id =
            exynosLayerMngRegisterClient(pScrn, client_name, 2);
        XDBG_RETURN_VAL_IF_FAIL(pPort->lyr_client_id != LYR_ERROR_ID,
                                BadRequest);
        pPort->lpos = LAYER_NONE;
        exynosLayerMngAddEvent(pPort->lyr_client_id, EVENT_LAYER_RELEASE,
                               _exynosVideoReleaseLayerCallback, pPort);
    }
#endif
    if (!_exynosVideoSupportID(id)) {
        XDBG_ERROR(MVDO, "'%c%c%c%c' not supported.\n", FOURCC_STR(id));
        return BadRequest;
    }

    TTRACE_VIDEO_BEGIN("XORG:XV:PUTIMAGE");

    XDBG_TRACE(MVDO, "======================================= \n");
    XDBG_DEBUG(MVDO, "src:(x%d,y%d w%d-h%d), dst:(x%d,y%d w%d-h%d)\n",
               src_x, src_y, src_w, src_h, dst_x, dst_y, dst_w, dst_h);
    XDBG_DEBUG(MVDO, "image size:(w%d-h%d) fourcc(%c%c%c%c)\n", width, height,
               FOURCC_STR(id));
    pPort->pScrn = pScrn;
    pPort->d.id = id;
    pPort->d.buf = buf;
    if (pExynos->xvperf_mode & XBERC_XVPERF_MODE_IA) {
        unsigned int keys[PLANAR_CNT] = { 0, };
        CARD32 cur, sub;
        char temp[64];

        cur = GetTimeInMillis();
        sub = cur - pPort->prev_time;
        pPort->prev_time = cur;
        temp[0] = '\0';
        if (IS_ZEROCOPY(id)) {
            _exynosVideoGetKeys(pPort, keys, NULL);
            snprintf(temp, sizeof(temp), "%d,%d,%d", keys[0], keys[1], keys[2]);
        }
        ErrorF("pPort(%p) put interval(%s) : %6" PRIXID " ms\n", pPort, temp,
               sub);
    }

    if (IS_ZEROCOPY(pPort->d.id)) {
        unsigned int keys[PLANAR_CNT] = { 0, };
        int i;

        if (_exynosVideoGetKeys(pPort, keys, NULL)) {
            TTRACE_VIDEO_END();
            return BadRequest;
        }

        for (i = 0; i < INBUF_NUM; i++)
            if (pPort->inbuf[i] && pPort->inbuf[i]->keys[0] == keys[0]) {
                XDBG_WARNING(MVDO, "got flink_id(%d) twice!\n", keys[0]);
                _exynosVideoSendReturnBufferMessage(pPort, NULL, keys);
                TTRACE_VIDEO_END();
                return Success;
            }
    }

    pPort->d.width = width;
    pPort->d.height = height;
    pPort->d.src.x = src_x;
    pPort->d.src.y = src_y;
    pPort->d.src.width = src_w;
    pPort->d.src.height = src_h;
    pPort->d.dst.x = dst_x;     /* included pDraw'x */
    pPort->d.dst.y = dst_y;     /* included pDraw'y */
    pPort->d.dst.width = dst_w;
    pPort->d.dst.height = dst_h;
    pPort->d.sync = FALSE;
    if (sync)
        XDBG_WARNING(MVDO, "not support sync.\n");
    pPort->d.data = data;
    pPort->d.pDraw = pDraw;
    if (clip_boxes) {
        if (!pPort->d.clip_boxes)
            pPort->d.clip_boxes = RegionCreate(NullBox, 0);
        RegionCopy(pPort->d.clip_boxes, clip_boxes);
    }

    old_drawing = pPort->drawing;
    pPort->drawing = _exynosVideodrawingOn(pPort);
    if (pDraw) {
        XDBG_DEBUG(MVDO, "pixmap:(x%d,y%d w%d-h%d) on:%d\n",
                   pDraw->x, pDraw->y, pDraw->width, pDraw->height,
                   pPort->drawing);
    }
    if (old_drawing != pPort->drawing) {
        _exynosVideoCloseConverter(pPort);
        _exynosVideoCloseOutBuffer(pPort, TRUE);

    }

    _exynosVideoGetRotation(pPort, &pPort->hw_rotate);

    if (pPort->drawing == ON_FB && pVideo->screen_rotate_degree > 0)
        exynosUtilRotateRect(pExynosMode->main_lcd_mode.hdisplay,
                             pExynosMode->main_lcd_mode.vdisplay,
                             &pPort->d.dst, pVideo->screen_rotate_degree);

    if (pPort->exynosure)
        if (pPort->drawing != ON_FB) {
            XDBG_ERROR(MVDO, "exynosure video should drawn on FB.\n");
            TTRACE_VIDEO_END();
            return BadRequest;
        }

    if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW)
        if (!_exynosVideoAddDrawableEvent(pPort)) {
            TTRACE_VIDEO_END();
            return BadRequest;
        }

    if (pPort->stream_cnt == 0) {
        pPort->stream_cnt++;

        if (pPort->preemption > -1)
            streaming_ports++;

        XDBG_SECURE(MVDO,
                    "pPort(%d) streams(%d) rotate(%d) flip(%d,%d) exynosure(%d) range(%d) usr_output(%x) on(%s)\n",
                    pPort->index, streaming_ports, pPort->rotate, pPort->hflip,
                    pPort->vflip, pPort->exynosure, pPort->csc_range,
                    pPort->usr_output, drawing_type[pPort->drawing]);
        XDBG_SECURE(MVDO,
                    "id(%c%c%c%c) sz(%dx%d) src(%d,%d %dx%d) dst(%d,%d %dx%d)\n",
                    FOURCC_STR(id), width, height, src_x, src_y, src_w, src_h,
                    dst_x, dst_y, dst_w, dst_h);

        if (streaming_ports > 1)
            _exynosVideoStopTvout(pPort->pScrn);
    }
    else if (pPort->stream_cnt == 1)
        pPort->stream_cnt++;

    int frame_changed = _exynosVideoCheckChange(pPort);

    switch (frame_changed) {
    case CHANGED_INPUT:
        if (pPort->cvt) {
            _exynosVideoCloseConverter(pPort);
            _exynosVideoCloseInBuffer(pPort);
            pPort->inbuf_is_fb = FALSE;
        }
        if (pPort->tv) {
            _exynosVideoUngrabTvout(pPort);
            _exynosVideoCloseInBuffer(pPort);
            pPort->inbuf_is_fb = FALSE;
            pPort->punched = FALSE;
        }
        break;
    case CHANGED_OUTPUT:
        if (pPort->cvt) {
            _exynosVideoCloseConverter(pPort);
            _exynosVideoCloseOutBuffer(pPort, FALSE);
            pPort->inbuf_is_fb = FALSE;
        }
        if (pPort->tv) {
            EXYNOSCvt *old_tv_cvt = exynosVideoTvGetConverter(pPort->tv);

            exynosVideoTvSetAttributes(pPort->tv, pPort->hw_rotate,
                                       pPort->hflip, pPort->vflip);
            if (exynosVideoTvResizeOutput
                (pPort->tv, &pPort->d.src, &pPort->d.dst)
                == TRUE) {
                EXYNOSCvt *new_tv_cvt = exynosVideoTvGetConverter(pPort->tv);

                if (new_tv_cvt != NULL) {
                    if (exynosCvtGetStamp(new_tv_cvt) !=
                        exynosCvtGetStamp(old_tv_cvt)) {
                        EXYNOSLayer *layer = exynosVideoTvGetLayer(pPort->tv);

                        /* TODO: Clear if fail */
                        XDBG_RETURN_VAL_IF_FAIL(layer != NULL, BadRequest);
                        exynosLayerRemoveNotifyFunc(layer,
                                                    _exynosVideoLayerNotifyFunc);
                        exynosLayerEnableVBlank(layer, FALSE);
                        exynosCvtAddCallback(new_tv_cvt,
                                             _exynosVideoTvoutCvtCallback,
                                             pPort);
                    }
                }
                else {
                    EXYNOSLayer *layer = exynosVideoTvGetLayer(pPort->tv);

                    /* TODO: Clear if fail */
                    XDBG_RETURN_VAL_IF_FAIL(layer != NULL, BadRequest);
                    exynosLayerEnableVBlank(layer, TRUE);
                    if (!exynosLayerExistNotifyFunc
                        (layer, _exynosVideoLayerNotifyFunc)) {
                        exynosLayerAddNotifyFunc(layer,
                                                 _exynosVideoLayerNotifyFunc,
                                                 pPort);
                    }
                }
            }
            else {
                exynosVideoTvDisconnect(pPort->tv);
                pPort->tv = NULL;
            }
            pPort->punched = FALSE;
            pPort->wait_vbuf = NULL;
        }
        break;
    case CHANGED_ALL:
        if (pPort->cvt) {
            _exynosVideoCloseConverter(pPort);
            _exynosVideoCloseOutBuffer(pPort, FALSE);
            _exynosVideoCloseInBuffer(pPort);
            pPort->inbuf_is_fb = FALSE;
        }
        if (pPort->tv) {
            _exynosVideoUngrabTvout(pPort);
            _exynosVideoCloseInBuffer(pPort);
            pPort->inbuf_is_fb = FALSE;
            pPort->punched = FALSE;
        }
        break;
    default:
        break;
    }

    if (!_exynosVideoCalculateSize(pPort)) {
        TTRACE_VIDEO_END();
        return BadRequest;
    }

    output = _exynosVideoGetTvoutMode(pPort);
    if (!(output & OUTPUT_LCD) && pPort->old_output & OUTPUT_LCD) {
        /* If the video of LCD becomes off, we also turn off LCD layer. */
        if (pPort->drawing == ON_PIXMAP || pPort->drawing == ON_WINDOW) {
            PixmapPtr pPixmap = _getPixmap(pPort->d.pDraw);
            EXYNOSPixmapPriv *privPixmap = exaGetPixmapDriverPrivate(pPixmap);

            exynosExaPrepareAccess(pPixmap, EXA_PREPARE_DEST);
            if (pPixmap->devPrivate.ptr && privPixmap->size > 0)
                memset(pPixmap->devPrivate.ptr, 0, privPixmap->size);
            exynosExaFinishAccess(pPixmap, EXA_PREPARE_DEST);

            DamageDamageRegion(pPort->d.pDraw, pPort->d.clip_boxes);
        }
        else {
            _exynosVideoCloseConverter(pPort);
            _exynosVideoCloseOutBuffer(pPort, TRUE);
        }
    }

    if (pPort->d.id == FOURCC_SR32 &&
        pPort->in_crop.width == pPort->out_crop.width &&
        pPort->in_crop.height == pPort->out_crop.height &&
        pPort->hw_rotate == 0)
        pPort->inbuf_is_fb = TRUE;
    else
        pPort->inbuf_is_fb = FALSE;

    inbuf = _exynosVideoGetInbuf(pPort);
    if (!inbuf) {
        TTRACE_VIDEO_END();
        return BadRequest;
    }

    /* punch here not only LCD but also HDMI. */
    if (pPort->drawing == ON_FB)
        _exynosVideoPunchDrawable(pPort);

    /* HDMI */
    if (output & OUTPUT_EXT)
        tvout = _exynosVideoPutImageTvout(pPort, output, inbuf);
    else {
        _exynosVideoUngrabTvout(pPort);

        EXYNOSWb *wb = exynosWbGet();

        if (wb)
            exynosWbSetSecure(wb, pPort->exynosure);
    }

    /* LCD */
    if (output & OUTPUT_LCD) {
        EXYNOSPtr pExynos = EXYNOSPTR(pScrn);

        if (pExynos->isLcdOff)
            XDBG_TRACE(MVDO, "port(%d) put image after dpms off.\n",
                       pPort->index);
        else if (pPort->inbuf_is_fb)
            lcdout = _exynosVideoPutImageInbuf(pPort, inbuf);
        else
            lcdout = _exynosVideoPutImageInternal(pPort, inbuf);
    }

    if (lcdout || tvout) {
        ret = Success;
    }
    else {
        if (IS_ZEROCOPY(pPort->d.id)) {
            int i;

            for (i = 0; i < INBUF_NUM; i++)
                if (pPort->inbuf[i] == inbuf) {
                    pPort->inbuf[i] = NULL;
                    exynosUtilRemoveFreeVideoBufferFunc(inbuf,
                                                        _exynosVideoFreeInbuf,
                                                        pPort);
                    break;
                }
            XDBG_WARNING_IF_FAIL(inbuf->ref_cnt == 1);
        }
        else
            XDBG_WARNING_IF_FAIL(inbuf->ref_cnt == 2);

        ret = BadRequest;
    }

    /* decrease ref_cnt here to pass ownership of inbuf to converter or tvout.
     * in case of zero-copy, it will be really freed
     * when converting is finished or tvout is finished.
     */
    exynosUtilVideoBufferUnref(inbuf);

    pPort->old_d = pPort->d;
    pPort->old_hflip = pPort->hflip;
    pPort->old_vflip = pPort->vflip;
    pPort->old_rotate = pPort->rotate;
    pPort->old_output = output;
    pPort->old_secure = pPort->exynosure;
    pPort->old_csc_range = pPort->csc_range;
#ifdef LAYER_MANAGER
    pPort->old_lpos = pPort->lpos;
#endif
    XDBG_TRACE(MVDO, "=======================================.. \n");

    TTRACE_VIDEO_END();

    return ret;
}

static int
EXYNOSVideoDDPutImage(ClientPtr client,
                      DrawablePtr pDraw,
                      XvPortPtr pPort,
                      GCPtr pGC,
                      INT16 src_x, INT16 src_y,
                      CARD16 src_w, CARD16 src_h,
                      INT16 drw_x, INT16 drw_y,
                      CARD16 drw_w, CARD16 drw_h,
                      XvImagePtr format,
                      unsigned char *data, Bool sync, CARD16 width,
                      CARD16 height)
{
    EXYNOSVideoPortInfo *info = _port_info(pDraw);
    int ret;

    if (info) {
        info->client = client;
        info->pp = pPort;
    }

    ret = ddPutImage(client, pDraw, pPort, pGC,
                     src_x, src_y, src_w, src_h,
                     drw_x, drw_y, drw_w, drw_h,
                     format, data, sync, width, height);

    return ret;
}

static void
EXYNOSVideoStop(ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    EXYNOSPortPrivPtr pPort = (EXYNOSPortPrivPtr) data;

    if (!exit)
        return;

    XDBG_DEBUG(MVDO, "exit (%d) \n", exit);

    _exynosVideoStreamOff(pPort);

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
exynosVideoSetupImageVideo(ScreenPtr pScreen)
{
    XF86VideoAdaptorPtr pAdaptor;
    EXYNOSPortPrivPtr pPort;
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    int i;

    pAdaptor = calloc(1, sizeof(XF86VideoAdaptorRec) +
                      (sizeof(DevUnion) +
                       sizeof(EXYNOSPortPriv)) * EXYNOS_MAX_PORT);
    if (!pAdaptor)
        return NULL;

    dummy_encoding[0].width = pScreen->width;
    dummy_encoding[0].height = pScreen->height;

    pAdaptor->type = XvWindowMask | XvPixmapMask | XvInputMask | XvImageMask;
    pAdaptor->flags = VIDEO_OVERLAID_IMAGES;
    pAdaptor->name = "EXYNOS supporting Software Video Conversions";
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
        pPort[i].usr_output = OUTPUT_LCD | OUTPUT_EXT | OUTPUT_FULL;
        pPort[i].outbuf_cvting = -1;
#ifdef LAYER_MANAGER
        pPort[i].lpos = LAYER_NONE;
        pPort[i].lyr_client_id = LYR_ERROR_ID;
#endif
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;
    pAdaptor->nImages = NUM_IMAGES;
    pAdaptor->pImages = images;

    pAdaptor->GetPortAttribute = EXYNOSVideoGetPortAttribute;
    pAdaptor->SetPortAttribute = EXYNOSVideoSetPortAttribute;
    pAdaptor->QueryBestSize = EXYNOSVideoQueryBestSize;
    pAdaptor->QueryImageAttributes = EXYNOSVideoQueryImageAttributes;
    pAdaptor->PutImage = EXYNOSVideoPutImage;
    pAdaptor->StopVideo = EXYNOSVideoStop;

    if (!_exynosVideoRegisterEventResourceTypes()) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "Failed to register EventResourceTypes. \n");
        return NULL;
    }
    return pAdaptor;
}

static void
EXYNOSVideoReplacePutImageFunc(ScreenPtr pScreen)
{
    int i;

    XvScreenPtr xvsp = dixLookupPrivate(&pScreen->devPrivates,
                                        XvGetScreenKey());

    if (!xvsp)
        return;

    for (i = 0; i < xvsp->nAdaptors; i++) {
        XvAdaptorPtr pAdapt = xvsp->pAdaptors + i;

        if (pAdapt->ddPutImage) {
            ddPutImage = pAdapt->ddPutImage;
            pAdapt->ddPutImage = EXYNOSVideoDDPutImage;
            break;
        }
    }

    if (!dixRegisterPrivateKey
        (VideoPortKey, PRIVATE_WINDOW, sizeof(EXYNOSVideoPortInfo)))
        return;
    if (!dixRegisterPrivateKey
        (VideoPortKey, PRIVATE_PIXMAP, sizeof(EXYNOSVideoPortInfo)))
        return;
}

#ifdef XV
/**
 * Set up everything we need for Xv.
 */
Bool
exynosVideoInit(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    EXYNOSVideoPrivPtr pVideo;

    TTRACE_VIDEO_BEGIN("XORG:XV:INIT");

    pVideo = (EXYNOSVideoPrivPtr) calloc(sizeof(EXYNOSVideoPriv), 1);
    if (!pVideo) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s. Can't alloc memory. %s:%d\n", __func__, __FILE__,
                   __LINE__);
        goto bail;
    }

    pVideo->pAdaptor[0] = exynosVideoSetupImageVideo(pScreen);
    if (!pVideo->pAdaptor[0]) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s. Can't setup adaptor 0\n", __func__);
        goto bail;
    }

    pVideo->pAdaptor[1] = exynosVideoSetupVirtualVideo(pScreen);
    if (!pVideo->pAdaptor[1]) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s. Can't setup adaptor 1\n", __func__);
        goto bail;
    }

    pVideo->pAdaptor[2] = exynosVideoSetupDisplayVideo(pScreen);
    if (!pVideo->pAdaptor[2]) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s. Can't setup adaptor 2\n", __func__);
        goto bail;
    }

    pVideo->pAdaptor[3] = exynosVideoSetupCloneVideo(pScreen);
    if (!pVideo->pAdaptor[3]) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                   "%s. Can't setup adaptor 3\n", __func__);
        goto bail;
    }

    if (!xf86XVScreenInit(pScreen, pVideo->pAdaptor, ADAPTOR_NUM)) {
        xf86DrvMsg(pScrn->scrnIndex, X_ERROR, "%s. Can't init XV\n", __func__);
        goto bail;
    }
/* TODO: Check error*/
    EXYNOSVideoReplacePutImageFunc(pScreen);
    exynosVirtualVideoReplacePutStillFunc(pScreen);

    if (registered_handler == FALSE) {
        if (!RegisterBlockAndWakeupHandlers(_exynosVideoBlockHandler,
                                            (WakeupHandlerProcPtr) NoopDDA,
                                            pScrn)) {
            xf86DrvMsg(pScrn->scrnIndex, X_ERROR,
                       "%s. Can't register handler\n", __func__);
            goto bail;
        }
        registered_handler = TRUE;
    }

    pExynos->pVideoPriv = pVideo;
    xorg_list_init(&layer_owners);

    TTRACE_VIDEO_END();

    return TRUE;
 bail:
    if (pVideo) {
        int i;

        for (i = 0; i < ADAPTOR_NUM; i++) {
            if (pVideo->pAdaptor[i]) {
                /* TODO: Free pPORT */
                free(pVideo->pAdaptor[i]);
                pVideo->pAdaptor[i] = NULL;
            }
        }
        free(pVideo);
    }
    TTRACE_VIDEO_END();
    return FALSE;
}

/**
 * Shut down Xv, used on regeneration.
 */
void
exynosVideoFini(ScreenPtr pScreen)
{
    ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    EXYNOSVideoPrivPtr pVideo = pExynos->pVideoPriv;
    EXYNOSPortPrivPtr pCur = NULL, pNext = NULL;
    int i;

    xorg_list_for_each_entry_safe(pCur, pNext, &layer_owners, link) {
        if (pCur->tv) {
            exynosVideoTvDisconnect(pCur->tv);
            pCur->tv = NULL;
        }

        if (pCur->d.clip_boxes) {
            RegionDestroy(pCur->d.clip_boxes);
            pCur->d.clip_boxes = NULL;
        }
    }

    for (i = 0; i < ADAPTOR_NUM; i++)
        if (pVideo->pAdaptor[i])
            free(pVideo->pAdaptor[i]);

    free(pVideo);
    pExynos->pVideoPriv = NULL;
}

#endif

void
exynosVideoDpms(ScrnInfoPtr pScrn, Bool on)
{
    if (!on) {
        EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
        XF86VideoAdaptorPtr pAdaptor = pExynos->pVideoPriv->pAdaptor[0];
        int i;

        for (i = 0; i < EXYNOS_MAX_PORT; i++) {
            EXYNOSPortPrivPtr pPort =
                (EXYNOSPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;
            if (pPort->stream_cnt == 0)
                continue;
            XDBG_TRACE(MVDO, "port(%d) cvt stop.\n", pPort->index);
            _exynosVideoCloseConverter(pPort);
            _exynosVideoCloseInBuffer(pPort);
        }
    }
}

void
exynosVideoScreenRotate(ScrnInfoPtr pScrn, int degree)
{
#ifndef LAYER_MANAGER
    EXYNOSPtr pExynos = EXYNOSPTR(pScrn);
    EXYNOSVideoPrivPtr pVideo = pExynos->pVideoPriv;
    int old_degree;

    if (pVideo->screen_rotate_degree == degree)
        return;

    old_degree = pVideo->screen_rotate_degree;
    pVideo->screen_rotate_degree = degree;
    XDBG_DEBUG(MVDO, "screen rotate degree: %d\n", degree);

    if (pExynos->isLcdOff)
        return;

    EXYNOSPortPrivPtr pCur = NULL, pNext = NULL;

    xorg_list_for_each_entry_safe(pCur, pNext, &layer_owners, link) {
        EXYNOSModePtr pExynosMode = pExynos->pExynosMode;
        EXYNOSVideoBuf *old_vbuf, *rot_vbuf;
        xRectangle rot_rect, dst_rect;
        int rot_width, rot_height;
        int scn_width, scn_height;
        int degree_diff = degree - old_degree;

        if (!pCur->layer)
            continue;

        old_vbuf = exynosLayerGetBuffer(pCur->layer);
        XDBG_RETURN_IF_FAIL(old_vbuf != NULL);

        rot_width = old_vbuf->width;
        rot_height = old_vbuf->height;
        rot_rect = old_vbuf->crop;
        exynosUtilRotateArea(&rot_width, &rot_height, &rot_rect, degree_diff);

        rot_vbuf =
            exynosUtilAllocVideoBuffer(pScrn, FOURCC_RGB32, rot_width,
                                       rot_height,
                                       (pExynos->scanout) ? TRUE : FALSE, FALSE,
                                       pCur->exynosure);
        XDBG_RETURN_IF_FAIL(rot_vbuf != NULL);
        rot_vbuf->crop = rot_rect;

        exynosUtilConvertBos(pScrn, 0,
                             old_vbuf->bo[0], old_vbuf->width, old_vbuf->height,
                             &old_vbuf->crop, old_vbuf->width * 4,
                             rot_vbuf->bo[0], rot_vbuf->width, rot_vbuf->height,
                             &rot_vbuf->crop, rot_vbuf->width * 4, FALSE,
                             degree_diff);

        tbm_bo_map(rot_vbuf->bo[0], TBM_DEVICE_2D, TBM_OPTION_READ);
        tbm_bo_unmap(rot_vbuf->bo[0]);

        exynosLayerGetRect(pCur->layer, NULL, &dst_rect);

        scn_width =
            (old_degree %
             180) ? pExynosMode->main_lcd_mode.
            vdisplay : pExynosMode->main_lcd_mode.hdisplay;
        scn_height =
            (old_degree %
             180) ? pExynosMode->main_lcd_mode.
            hdisplay : pExynosMode->main_lcd_mode.vdisplay;

        exynosUtilRotateRect(scn_width, scn_height, &dst_rect, degree_diff);

        exynosLayerFreezeUpdate(pCur->layer, TRUE);
        exynosLayerSetRect(pCur->layer, &rot_vbuf->crop, &dst_rect);
        exynosLayerFreezeUpdate(pCur->layer, FALSE);
        exynosLayerSetBuffer(pCur->layer, rot_vbuf);

        exynosUtilVideoBufferUnref(rot_vbuf);

        _exynosVideoCloseConverter(pCur);
    }
#endif
}

void
exynosVideoSwapLayers(ScreenPtr pScreen)
{
    EXYNOSPortPrivPtr pCur = NULL, pNext = NULL;
    EXYNOSPortPrivPtr pPort1 = NULL, pPort2 = NULL;

    xorg_list_for_each_entry_safe(pCur, pNext, &layer_owners, link) {
        if (!pPort1)
            pPort1 = pCur;
        else if (!pPort2)
            pPort2 = pCur;
    }

    if (pPort1 && pPort2) {
#ifndef LAYER_MANAGER
        exynosLayerSwapPos(pPort1->layer, pPort2->layer);
        XDBG_TRACE(MVDO, "%p : %p \n", pPort1->layer, pPort2->layer);
#else
        exynosLayerMngSwapPos(pPort1->lyr_client_id, pPort1->output,
                              pPort1->lpos, pPort2->lyr_client_id,
                              pPort2->output, pPort2->lpos);
        XDBG_TRACE(MVDO, "SWAP %d : %d \n", pPort1->lpos, pPort2->lpos);
#endif
    }
}

Bool
exynosVideoIsSecureMode(ScrnInfoPtr pScrn)
{
    EXYNOSPtr pExynos = (EXYNOSPtr) pScrn->driverPrivate;
    XF86VideoAdaptorPtr pAdaptor = pExynos->pVideoPriv->pAdaptor[0];
    int i;

    for (i = 0; i < EXYNOS_MAX_PORT; i++) {
        EXYNOSPortPrivPtr pPort =
            (EXYNOSPortPrivPtr) pAdaptor->pPortPrivates[i].ptr;

        if (pPort->exynosure) {
            XDBG_TRACE(MVDO, "pPort(%d) is exynosure.\n", pPort->index);
            return TRUE;
        }
    }

    XDBG_TRACE(MVDO, "no exynosure port.\n");

    return FALSE;
}
