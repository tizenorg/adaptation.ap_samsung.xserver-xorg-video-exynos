/* exynos_hwc.h
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

#ifndef _EXYNOS_HWC_H_
#define _EXYNOS_HWC_H_

#include "exynos_layer.h"
#include "exynos_layer_manager.h"

//EXYNOSLayer  *EXYNOSLayerPtr;
typedef struct _exynosHwcRec EXYNOSHwcRec, *EXYNOSHwcPtr;

typedef struct _HwcDrawableInfo {
    //  XID            id; /* xid in layer structuer*/
    ScrnInfoPtr pScrn;          /* pScrn is layer structure ?? */
    RESTYPE type;
} EXYNOSHwcDrawableInfo;

Bool exynosHwcIsDrawExist(DrawablePtr pDraw);
EXYNOSLayerPos exynosHwcGetDrawLpos(DrawablePtr pDraw);

Bool exynosHwcInit(ScreenPtr pScreen);
void exynosHwcDeinit(ScreenPtr pScreen);

/* get the screen pixmap of which is the result of composition of the overlays */
PixmapPtr exynosHwcGetScreenPixmap(ScreenPtr pScreen);

/* set HWC drawable of root window */
Bool exynosHwcDoSetRootDrawables(EXYNOSHwcPtr pHwc, PixmapPtr pScreenPixmap);

int EXYNOSHwcSetDrawables(ScreenPtr pScreen, DrawablePtr *pDraws,
                          xRectangle *srcRects, xRectangle *dstRects,
                          int count);
void exynosHwcUpdate(ScrnInfoPtr pScrn);
void exynosHwcSetDriFlag(DrawablePtr pDraw, Bool flag);
Bool exynosHwcGetDriFlag(DrawablePtr pDraw);

EXYNOSLayerMngClientID exynosHwcGetLyrClientId(EXYNOSHwcPtr pHwc);

#endif                          // _EXYNOS_HWC_H_
