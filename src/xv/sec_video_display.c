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

#include <xf86xv.h>

#include "sec.h"
#include "sec_util.h"
#include "sec_video_display.h"
#include "sec_video_fourcc.h"
#include "sec_layer.h"

#include "fimg2d.h"
#ifdef LAYER_MANAGER
#include "sec_layer_manager.h"
#endif

#define SEC_MAX_PORT        1
#define LAYER_BUF_CNT       3

static XF86VideoEncodingRec dummy_encoding[] =
{
    { 0, "XV_IMAGE", -1, -1, { 1, 1 } },
    { 1, "XV_IMAGE", 4224, 4224, { 1, 1 } },
};

static XF86VideoFormatRec formats[] =
{
    { 16, TrueColor },
    { 24, TrueColor },
    { 32, TrueColor },
};

static XF86AttributeRec attributes[] =
{
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_OVERLAY" },
};

typedef enum
{
    PAA_MIN,
    PAA_OVERLAY,
    PAA_MAX
} SECPortAttrAtom;

static struct
{
    SECPortAttrAtom  paa;
    const char      *name;
    Atom             atom;
} atoms[] =
{
    { PAA_OVERLAY, "_USER_WM_PORT_ATTRIBUTE_OVERLAY", None },
};

typedef struct _GetData
{
    int width;
    int height;
    xRectangle   src;
    xRectangle   dst;
    DrawablePtr  pDraw;
} GetData;

/* SEC port information structure */
typedef struct
{
    int index;

    /* attributes */
    Bool overlay;

    ScrnInfoPtr pScrn;
    GetData d;
    GetData old_d;
#ifndef LAYER_MANAGER
    /* layer */
    SECLayer *layer;
#else
    SECLayerMngClientID lyr_client_id;
    SECLayerPos lpos;
    SECLayerOutput output;
#endif
    SECVideoBuf *outbuf[LAYER_BUF_CNT];


    int    stream_cnt;
    struct xorg_list link;
} SECPortPriv, *SECPortPrivPtr;

static RESTYPE event_drawable_type;

typedef struct _SECDisplayVideoResource
{
    XID id;
    RESTYPE type;

    SECPortPrivPtr pPort;
    ScrnInfoPtr pScrn;
} SECDisplayVideoResource;

static void _secDisplayVideoCloseBuffers (SECPortPrivPtr pPort);
static void SECDisplayVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit);

#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

static PixmapPtr
_getPixmap (DrawablePtr pDraw)
{
    if (pDraw->type == DRAWABLE_WINDOW)
        return pDraw->pScreen->GetWindowPixmap ((WindowPtr) pDraw);
    else
        return (PixmapPtr) pDraw;
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

    XDBG_ERROR (MDA, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static void
_copyBuffer (DrawablePtr pDraw, SECVideoBuf *vbuf)
{
    PixmapPtr pPixmap = (PixmapPtr)_getPixmap (pDraw);
    SECPixmapPriv *privPixmap = exaGetPixmapDriverPrivate (pPixmap);
    Bool need_finish = FALSE;
    tbm_bo_handle bo_handle_src, bo_handle_dst;
    G2dImage *src_image = NULL, *dst_image = NULL;

    if (!privPixmap->bo)
    {
        need_finish = TRUE;
        secExaPrepareAccess (pPixmap, EXA_PREPARE_DEST);
        XDBG_GOTO_IF_FAIL (privPixmap->bo != NULL, done_copy_buf);
    }

    bo_handle_src = tbm_bo_get_handle (privPixmap->bo, TBM_DEVICE_DEFAULT);
    XDBG_GOTO_IF_FAIL (bo_handle_src.u32 > 0, done_copy_buf);

    bo_handle_dst = tbm_bo_get_handle (vbuf->bo[0], TBM_DEVICE_DEFAULT);
    XDBG_GOTO_IF_FAIL (bo_handle_dst.u32 > 0, done_copy_buf);

    src_image = g2d_image_create_bo2 (G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB,
                                      (unsigned int)pDraw->width,
                                      (unsigned int)pDraw->height,
                                      (unsigned int)bo_handle_src.u32,
                                      (unsigned int)0,
                                      (unsigned int)pDraw->width * 4);
    XDBG_GOTO_IF_FAIL (src_image != NULL, done_copy_buf);

    dst_image = g2d_image_create_bo2 (G2D_COLOR_FMT_ARGB8888 | G2D_ORDER_AXRGB,
                                      (unsigned int)vbuf->width,
                                      (unsigned int)vbuf->height,
                                      (unsigned int)vbuf->handles[0],
                                      (unsigned int)vbuf->handles[1],
                                      (unsigned int)vbuf->pitches[0]);
    XDBG_GOTO_IF_FAIL (dst_image != NULL, done_copy_buf);

    tbm_bo_map (privPixmap->bo, TBM_DEVICE_MM, TBM_OPTION_READ);
    tbm_bo_map (vbuf->bo[0], TBM_DEVICE_MM, TBM_OPTION_READ);

    util_g2d_blend_with_scale (G2D_OP_SRC, src_image, dst_image,
                               (int)vbuf->crop.x, (int)vbuf->crop.y,
                               (unsigned int)vbuf->crop.width,
                               (unsigned int)vbuf->crop.height,
                               (int)vbuf->crop.x, (int)vbuf->crop.y,
                               (unsigned int)vbuf->crop.width,
                               (unsigned int)vbuf->crop.height,
                               FALSE);
    g2d_exec();

    tbm_bo_unmap (privPixmap->bo);
    tbm_bo_unmap (vbuf->bo[0]);

done_copy_buf:
    if (src_image)
        g2d_image_free (src_image);
    if (dst_image)
        g2d_image_free (dst_image);
    if (need_finish)
        secExaFinishAccess (pPixmap, EXA_PREPARE_DEST);
}

static Bool
_secDisplayVideoShowLayer (SECPortPrivPtr pPort, SECVideoBuf *vbuf,
                           SECLayerOutput output, SECLayerPos lpos)
{
#ifndef LAYER_MANAGER
    xRectangle src_rect, dst_rect;
    if (!pPort->layer)
    {
        SECModePtr pSecMode = (SECModePtr) SECPTR (pPort->pScrn)->pSecMode;

        if (!secUtilEnsureExternalCrtc (pPort->pScrn))
        {
            XDBG_ERROR (MDA, "failed : pPort(%d) connect external crtc\n", pPort->index);
            return FALSE;
        }

        pPort->layer = secLayerCreate (pPort->pScrn, output, lpos);
        XDBG_RETURN_VAL_IF_FAIL (pPort->layer != NULL, FALSE);

        if (output == LAYER_OUTPUT_EXT && pSecMode->conn_mode != DISPLAY_CONN_MODE_VIRTUAL)
            secLayerEnableVBlank (pPort->layer, TRUE);
    }

    secLayerGetRect (pPort->layer, &src_rect, &dst_rect);

    if (memcmp (&pPort->d.src, &src_rect, sizeof (xRectangle)) ||
        memcmp (&pPort->d.dst, &dst_rect, sizeof (xRectangle)))
    {
        secLayerFreezeUpdate (pPort->layer, TRUE);
        secLayerSetRect (pPort->layer, &pPort->d.src, &pPort->d.dst);
        secLayerFreezeUpdate (pPort->layer, FALSE);
    }

    secLayerSetBuffer (pPort->layer, vbuf);
    if (!secLayerIsVisible (pPort->layer))
        secLayerShow (pPort->layer);
#else
   if (pPort->lpos == LAYER_NONE)
   {
       if (!secUtilEnsureExternalCrtc (pPort->pScrn))
       {
           XDBG_ERROR (MDA, "failed : pPort(%d) connect external crtc\n", pPort->index);
           return FALSE;
       }
       if (secLayerMngSet(pPort->lyr_client_id, 0, 0, &pPort->d.src, &pPort->d.dst, NULL, vbuf,
                          output, lpos, NULL, NULL))
       {
           pPort->lpos = lpos;
           pPort->output = output;
       }
       else
       {
           XDBG_ERROR (MDA, "Can't set layer %d\n", pPort->index);
           return FALSE;
       }

   }
#endif
    XDBG_DEBUG (MDA, "pDraw(0x%lx), fb_id(%d), (%d,%d %dx%d) (%d,%d %dx%d)\n",
                pPort->d.pDraw->id, vbuf->fb_id,
                pPort->d.src.x, pPort->d.src.y, pPort->d.src.width, pPort->d.src.height,
                pPort->d.dst.x, pPort->d.dst.y, pPort->d.dst.width, pPort->d.dst.height);

    return TRUE;
}

static void
_secDisplayVideoHideLayer (SECPortPrivPtr pPort)
{
#ifndef LAYER_MANAGER
    if (!pPort->layer)
        return;

    secLayerUnref (pPort->layer);
    pPort->layer = NULL;
#else
    if (pPort->lpos != LAYER_NONE)
    {
        secLayerMngRelease(pPort->lyr_client_id, pPort->output, pPort->lpos);
        pPort->lpos = LAYER_NONE;
    }
#endif
}

static SECVideoBuf*
_secDisplayVideoGetBuffer (SECPortPrivPtr pPort)
{
    int i;

    for (i = 0; i < LAYER_BUF_CNT; i++)
    {
        if (!pPort->outbuf[i])
        {
            SECPtr pSec = SECPTR (pPort->pScrn);

            pPort->outbuf[i] = secUtilAllocVideoBuffer (pPort->pScrn, FOURCC_RGB32,
                                                        pPort->d.pDraw->width,
                                                        pPort->d.pDraw->height,
                                                        (pSec->scanout)?TRUE:FALSE,
                                                        FALSE, FALSE);
            XDBG_GOTO_IF_FAIL (pPort->outbuf[i] != NULL, fail_get_buf);
            break;
        }
        else if (!pPort->outbuf[i]->showing)
            break;
    }

    if (i == LAYER_BUF_CNT)
    {
        XDBG_ERROR (MDA, "now all outbufs in use!\n");
        return NULL;
    }

    XDBG_DEBUG (MDA, "outbuf: stamp(%"PRIuPTR") index(%d) h(%d,%d,%d)\n",
                pPort->outbuf[i]->stamp, i,
                pPort->outbuf[i]->handles[0],
                pPort->outbuf[i]->handles[1],
                pPort->outbuf[i]->handles[2]);

    pPort->outbuf[i]->crop = pPort->d.src;

    _copyBuffer (pPort->d.pDraw, pPort->outbuf[i]);

    return pPort->outbuf[i];

fail_get_buf:
    _secDisplayVideoCloseBuffers (pPort);
    return NULL;
}

static void
_secDisplayVideoCloseBuffers (SECPortPrivPtr pPort)
{
    int i;

    for (i = 0; i < LAYER_BUF_CNT; i++)
        if (pPort->outbuf[i])
        {
            secUtilVideoBufferUnref (pPort->outbuf[i]);
            pPort->outbuf[i] = NULL;
        }
}

static void
_secDisplayVideoStreamOff (SECPortPrivPtr pPort)
{
    _secDisplayVideoHideLayer (pPort);
    _secDisplayVideoCloseBuffers (pPort);
#ifdef LAYER_MANAGER
    secLayerMngUnRegisterClient(pPort->lyr_client_id);
    pPort->lyr_client_id = LYR_ERROR_ID;
#endif
    memset (&pPort->old_d, 0, sizeof (GetData));
    memset (&pPort->d, 0, sizeof (GetData));

    if (pPort->stream_cnt > 0)
    {
        pPort->stream_cnt = 0;
        XDBG_SECURE (MDA, "pPort(%d) stream off. \n", pPort->index);
    }

    XDBG_TRACE (MDA, "done. \n");
}

static Bool
_secDisplayVideoAddDrawableEvent (SECPortPrivPtr pPort)
{
    SECDisplayVideoResource *resource;
    void *ptr=NULL;
    int ret;

    ret = dixLookupResourceByType (&ptr, pPort->d.pDraw->id,
                                   event_drawable_type, NULL, DixWriteAccess);
    if (ret == Success)
    {
        return TRUE;
    }

    resource = malloc (sizeof (SECDisplayVideoResource));
    if (resource == NULL)
        return FALSE;

    if (!AddResource (pPort->d.pDraw->id, event_drawable_type, resource))
    {
        free (resource);
        return FALSE;
    }

    XDBG_TRACE (MDA, "id(0x%08lx). \n", pPort->d.pDraw->id);

    resource->id = pPort->d.pDraw->id;
    resource->type = event_drawable_type;
    resource->pPort = pPort;
    resource->pScrn = pPort->pScrn;

    return TRUE;
}

static int
_secDisplayVideoRegisterEventDrawableGone (void *data, XID id)
{
    SECDisplayVideoResource *resource = (SECDisplayVideoResource*)data;

    XDBG_TRACE (MDA, "id(0x%08lx). \n", id);

    if (!resource)
        return Success;

    if (!resource->pPort || !resource->pScrn)
        return Success;

    SECDisplayVideoStop (resource->pScrn, (pointer)resource->pPort, 1);

    free(resource);

    return Success;
}

static Bool
_secDisplayVideoRegisterEventResourceTypes (void)
{
    event_drawable_type = CreateNewResourceType (_secDisplayVideoRegisterEventDrawableGone, "Sec Video Drawable");

    if (!event_drawable_type)
        return FALSE;

    return TRUE;
}

static int
SECDisplayVideoGetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32      *value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_OVERLAY))
    {
        *value = pPort->overlay;
        return Success;
    }

    return BadMatch;
}

static int
SECDisplayVideoSetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32       value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_OVERLAY))
    {
        pPort->overlay = value;
        XDBG_DEBUG (MDA, "overlay(%d) \n", (int) value);
        return Success;
    }

    return BadMatch;
}

static int
SECDisplayVideoGetStill (ScrnInfoPtr pScrn,
                         short vid_x, short vid_y, short drw_x, short drw_y,
                         short vid_w, short vid_h, short drw_w, short drw_h,
                         RegionPtr clipBoxes, pointer data,
                         DrawablePtr pDraw)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;
    SECVideoBuf *vbuf;
    SECLayerOutput output;
    SECLayerPos lpos;

    XDBG_RETURN_VAL_IF_FAIL (pDraw->type == DRAWABLE_PIXMAP, BadRequest);

    pPort->pScrn = pScrn;

    pPort->d.width = pDraw->width;
    pPort->d.height = pDraw->height;
    pPort->d.src.x = drw_x;
    pPort->d.src.y = drw_y;
    pPort->d.src.width = drw_w;
    pPort->d.src.height = drw_h;
    pPort->d.dst.x = vid_x;
    pPort->d.dst.y = vid_y;
    pPort->d.dst.width = drw_w;
    pPort->d.dst.height = drw_h;
    pPort->d.pDraw = pDraw;
#ifdef LAYER_MANAGER
    if (pPort->lyr_client_id == LYR_ERROR_ID)
    {
        char client_name[20] = {0};
        if (sprintf(client_name, "XV_PUTSTILL-%d", pPort->index) < 0)
        {
            XDBG_ERROR(MDA, "Can't register layer manager client\n");
            return BadRequest;
        }
        pPort->lyr_client_id = secLayerMngRegisterClient(pScrn, client_name, 2);
        XDBG_RETURN_VAL_IF_FAIL (pPort->lyr_client_id != LYR_ERROR_ID, BadRequest);
        pPort->lpos = LAYER_NONE;
    }
#endif
    if (pPort->old_d.width != pPort->d.width ||
        pPort->old_d.height != pPort->d.height)
    {
        _secDisplayVideoHideLayer (pPort);
        _secDisplayVideoCloseBuffers (pPort);
    }

    if (!_secDisplayVideoAddDrawableEvent (pPort))
        return BadRequest;

    if (pPort->stream_cnt == 0)
    {
        pPort->stream_cnt++;

        XDBG_SECURE (MDA, "pPort(%d) sz(%dx%d) src(%d,%d %dx%d) dst(%d,%d %dx%d)\n",
                     pPort->index, pDraw->width, pDraw->height,
                     drw_x, drw_y, drw_w, drw_h, vid_x, vid_y, vid_w, vid_h);
    }

    vbuf = _secDisplayVideoGetBuffer (pPort);
    XDBG_RETURN_VAL_IF_FAIL (vbuf != NULL, BadRequest);

    if (pPort->overlay)
    {
        output = LAYER_OUTPUT_EXT;
        lpos = LAYER_UPPER;
    }
    else
    {
        XDBG_ERROR (MDA, "pPort(%d) implemented for only overlay\n", pPort->index);
        return BadRequest;
    }

    if (!_secDisplayVideoShowLayer (pPort, vbuf, output, lpos))
        return BadRequest;

    pPort->old_d = pPort->d;

    return Success;
}

static void
SECDisplayVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    XDBG_TRACE (MDA, "exit (%d) \n", exit);

    if (!exit)
        return;

    _secDisplayVideoStreamOff (pPort);

    pPort->overlay = FALSE;
}

static void
SECDisplayVideoQueryBestSize (ScrnInfoPtr pScrn,
                              Bool motion,
                              short vid_w, short vid_h,
                              short dst_w, short dst_h,
                              unsigned int *p_w, unsigned int *p_h,
                              pointer data)
{
    if (p_w)
        *p_w = (unsigned int)dst_w & ~1;
    if (p_h)
        *p_h = (unsigned int)dst_h;
}

XF86VideoAdaptorPtr
secVideoSetupDisplayVideo (ScreenPtr pScreen)
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

    pAdaptor->type = XvPixmapMask | XvOutputMask | XvStillMask;
    pAdaptor->flags = VIDEO_OVERLAID_IMAGES;
    pAdaptor->name = "SEC External Overlay Video";
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
#ifdef LAYER_MANAGER
        pPort[i].lpos = LAYER_NONE;
        pPort[i].lyr_client_id = LYR_ERROR_ID;
#endif
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;

    pAdaptor->GetPortAttribute     = SECDisplayVideoGetPortAttribute;
    pAdaptor->SetPortAttribute     = SECDisplayVideoSetPortAttribute;
    pAdaptor->GetStill             = SECDisplayVideoGetStill;
    pAdaptor->StopVideo            = SECDisplayVideoStop;
    pAdaptor->QueryBestSize        = SECDisplayVideoQueryBestSize;

    if (!_secDisplayVideoRegisterEventResourceTypes ())
    {
        ScrnInfoPtr pScrn = xf86Screens[pScreen->myNum];
        xf86DrvMsg (pScrn->scrnIndex, X_ERROR, "Failed to register EventResourceTypes. \n");
        return FALSE;
    }

    return pAdaptor;
}
