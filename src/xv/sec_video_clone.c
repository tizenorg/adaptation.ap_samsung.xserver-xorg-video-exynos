/*
 * xserver-xorg-video-exynos
 *
 * Copyright 2014 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.
 *
 * Contact: Junkyeong Kim <jk0430.kim@samsung.com>
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
#include "sec_video_clone.h"
#include "sec_video_fourcc.h"
#include "sec_layer.h"

#include "fimg2d.h"

#define SEC_MAX_PORT        1
#define LAYER_BUF_CNT       3
#define OUTBUF_NUM          3

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
    { 0, 0, 1, "_USER_WM_PORT_ATTRIBUTE_CLONE" },
};

typedef enum
{
    PAA_MIN,
    PAA_FREEZE,
    PAA_ROTATE,
    PAA_MAX
} SECPortAttrAtom;

static struct
{
    SECPortAttrAtom  paa;
    const char      *name;
    Atom             atom;
} atoms[] =
{
    { PAA_FREEZE, "_USER_WM_PORT_ATTRIBUTE_FREEZE", None },
    { PAA_ROTATE, "_USER_WM_PORT_ATTRIBUTE_ROTATE", None },
};

/* SEC port information structure */
typedef struct
{
    int index;

    /* attributes */
    Bool freeze;
    int degree;
    ScrnInfoPtr pScrn;

    /* writeback */
    SECWb *wb;

    struct xorg_list link;
} SECPortPriv, *SECPortPrivPtr;

typedef struct _SECCloneVideoResource
{
    XID id;
    RESTYPE type;

    SECPortPrivPtr pPort;
    ScrnInfoPtr pScrn;
} SECCloneVideoResource;

#define NUM_FORMATS       (sizeof(formats) / sizeof(formats[0]))
#define NUM_ATTRIBUTES    (sizeof(attributes) / sizeof(attributes[0]))
#define NUM_ATOMS         (sizeof(atoms) / sizeof(atoms[0]))

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

    XDBG_ERROR (MCLON, "Error: Unknown Port Attribute Name!\n");

    return None;
}

static int
SECCloneVideoGetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32      *value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_FREEZE)) {
        *value = pPort->freeze;
        return Success;
    } else if (attribute == _portAtom (PAA_ROTATE)) {
        *value = pPort->degree;
        return Success;
    }

    return BadMatch;
}

static int
SECCloneVideoSetPortAttribute (ScrnInfoPtr pScrn,
                                 Atom        attribute,
                                 INT32       value,
                                 pointer     data)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    if (attribute == _portAtom (PAA_FREEZE)) {
        if (!pPort->wb) {
            XDBG_ERROR (MCLON, "do PutVideo first\n");
            return BadMatch;
        }
        if (pPort->freeze == value) {
            XDBG_WARNING (MCLON, "same freeze cmd(%d)\n", (int)value);
            return Success;
        }

        pPort->freeze = value;
        XDBG_DEBUG (MCLON, "freeze(%d)\n", (int)value);

        if (value == 1)
            secWbPause(pPort->wb);
        else
            secWbResume(pPort->wb);
        return Success;
    } else if (attribute == _portAtom (PAA_ROTATE)) {
        if (pPort->degree != value) {
            pPort->degree = value;
            if (pPort->wb) {
                XDBG_DEBUG (MCLON, "rotate(%d)\n", (int)value);
                secWbSetRotate (pPort->wb, pPort->degree);
            }
        }

        return Success;
    }

    return BadMatch;
}

static int
SECCloneVideoPutVideo (ScrnInfoPtr pScrn,
                         short vid_x, short vid_y, short drw_x, short drw_y,
                         short vid_w, short vid_h, short drw_w, short drw_h,
                         RegionPtr clipBoxes, pointer data,
                         DrawablePtr pDraw)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;
    SECPtr pSec = SECPTR (pScrn);
    SECModePtr pSecMode = pSec->pSecMode;
    int wb_hz;

    XDBG_DEBUG (MCLON, "SECCloneVideoPutVideo start\n");

    if (pDraw->type != DRAWABLE_WINDOW) {
        XDBG_ERROR (MCLON, "Fail : pDraw->type is not DRAWABLE_WINDOW.\n");
        return BadRequest;
    }

    pPort->pScrn = pScrn;

    if (pPort->wb) {
        XDBG_ERROR (MCLON, "Fail : wb(%p) already exists.\n", pPort->wb);
        return BadRequest;
    }

    if (secWbIsOpened()) {
        XDBG_ERROR (MCLON, "Fail : wb(%d) already opened.\n", secWbGet());
        return BadRequest;
    }

    wb_hz = (pSec->wb_hz > 0)?pSec->wb_hz:pSecMode->ext_connector_mode.vrefresh;
    XDBG_TRACE (MCLON, "wb_hz(%d) vrefresh(%d)\n",
        pSec->wb_hz, pSecMode->ext_connector_mode.vrefresh);

    pPort->wb = secWbOpen(pPort->pScrn, FOURCC_ST12,
            pDraw->width, pDraw->height,
            pSec->scanout, wb_hz, FALSE);
    XDBG_RETURN_VAL_IF_FAIL(pPort->wb != NULL, BadAlloc);

    if (pPort->wb) {
        secWbSetRotate (pPort->wb, pPort->degree);
        XDBG_DEBUG (MCLON, "pPort->degree : %d\n", pPort->degree);
        secWbSetTvout (pPort->wb, TRUE);

        XDBG_TRACE(MCLON, "wb(%p) start.\n", pPort->wb);
        if (!secWbStart (pPort->wb)) {
            XDBG_ERROR(MCLON, "wb(%p) start fail.\n", pPort->wb);
            secWbClose (pSec->wb_clone);
            pPort->wb = NULL;
            return BadDrawable;
        }
    }

    if (!secWbIsRunning()) {
        XDBG_WARNING (MCLON, "wb is stopped.\n");
        return BadRequest;
    }

    XDBG_TRACE (MCLON, "wb(%p), running(%d).\n", pPort->wb, secWbIsRunning());

    return Success;
}

static void
SECCloneVideoStop (ScrnInfoPtr pScrn, pointer data, Bool exit)
{
    SECPortPrivPtr pPort = (SECPortPrivPtr) data;

    XDBG_TRACE (MCLON, "exit (%d) \n", exit);

    if (!exit)
        return;

    if (pPort->wb) {
        secWbClose(pPort->wb);
        pPort->wb = NULL;
        XDBG_TRACE (MCLON, "excuted\n");
    }
    pPort->freeze = 0;
    pPort->degree = 0;
}

static void
SECCloneVideoQueryBestSize (ScrnInfoPtr pScrn,
                              Bool motion,
                              short vid_w, short vid_h,
                              short dst_w, short dst_h,
                              unsigned int *p_w, unsigned int *p_h,
                              pointer data)
{
    if (p_w)
        *p_w = (unsigned int)dst_w & (~0x1);
    if (p_h)
        *p_h = (unsigned int)dst_h & (~0x1);
}

XF86VideoAdaptorPtr
secVideoSetupCloneVideo (ScreenPtr pScreen)
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

    pAdaptor->type = XvWindowMask | XvInputMask | XvVideoMask;
    pAdaptor->flags = VIDEO_OVERLAID_IMAGES;
    pAdaptor->name = "SEC Clone Video";
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
        pPort[i].freeze = 0;
        pPort[i].degree = 0;
    }

    pAdaptor->nAttributes = NUM_ATTRIBUTES;
    pAdaptor->pAttributes = attributes;

    pAdaptor->GetPortAttribute     = SECCloneVideoGetPortAttribute;
    pAdaptor->SetPortAttribute     = SECCloneVideoSetPortAttribute;
    pAdaptor->PutVideo             = SECCloneVideoPutVideo;
    pAdaptor->StopVideo            = SECCloneVideoStop;
    pAdaptor->QueryBestSize        = SECCloneVideoQueryBestSize;

    return pAdaptor;
}
