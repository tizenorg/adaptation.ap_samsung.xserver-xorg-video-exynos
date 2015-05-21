/* sec_hwc.h
 *
 * Copyright (c) 2009, 2013 The Linux Foundation. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of The Linux Foundation nor
 *       the names of its contributors may be used to endorse or promote
 *       products derived from this software without specific prior written
 *       permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef _SEC_HWC_H_
#define _SEC_HWC_H_

#include "sec_layer.h"

//SECLayer  *SECLayerPtr;
typedef struct _secHwcRec SECHwcRec, *SECHwcPtr;


/* status for the usable layers to be existed */
typedef enum {
    SEC_HWC_STATUS_NULL,
    SEC_HWC_STATUS_AVAIL,
    SEC_HWC_STATUS_READY,
    SEC_HWC_STATUS_FULL,
} SECHwcStatus;

typedef struct _HwcDrawableInfo
{
  //  XID            id; /* xid in layer structuer*/
    ScrnInfoPtr    pScrn; /* pScrn is layer structure ??*/
    RESTYPE        type;
//    SECLayerPtr    pLayer;
} SECHwcDrawableInfo;


Bool secHwcIsDrawExist (DrawablePtr pDraw);
SECLayerPos secHwcGetDrawLpos (DrawablePtr pDraw);

Bool secHwcInit (ScreenPtr pScreen);

void secHwcDeinit (ScreenPtr pScreen);

///* get overlay with drawable */
//SECOverlayPtr secHwcGetOverlayFromDraw (secHwcPtr pHwc, DrawablePtr pDraw);
//
///* check the hwc status */
//SECHwcStatus secHwcCheckStatus (secHwcPtr pHwc);
//
///* occupy the driver overlay, i.e. overlay for xv */
//SECHwcStatus secHwcOccupyDrvOverlay (secHwcPtr pHwc);
//
///* return the occupied overlay */
//void secHwcReturnDrvOverlay (secHwcPtr pHwc);
//
///* set the configuration of hwc when the external display is connected */
//void secHwcSetExtDisplay (secHwcPtr pHwc, Bool enable);
//
///* set the initial overlay of the lcd at the launching time */
//void secHwcSetDefaultOverlay (secHwcPtr pHwc, secOverlayPtr pOverlay);
//
/* get the screen pixmap of which is the result of composition of the overlays */
PixmapPtr secHwcGetScreenPixmap (ScreenPtr pScreen);

/* set HWC drawable of root window */
Bool secHwcDoSetRootDrawables(SECHwcPtr pHwc, PixmapPtr pScreenPixmap);
//int secHwcGetCount (secHwcPtr pHwc);

int SECHwcSetDrawables (ScreenPtr pScreen, DrawablePtr *pDraws, xRectangle *srcRects, xRectangle *dstRects, int count);
void secHwcUpdate (ScrnInfoPtr pScrn);
void secHwcSetDriFlag (DrawablePtr pDraw, Bool flag);
Bool secHwcGetDriFlag (DrawablePtr pDraw);

#endif // _SEC_HWC_H_
