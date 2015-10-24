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

#ifndef __SEC_LAYER_H__
#define __SEC_LAYER_H__

#include "exynos_video_types.h"
#include "exynos_video_fourcc.h"
#include <xf86Crtc.h>

typedef enum {
    LAYER_OUTPUT_LCD,
    LAYER_OUTPUT_EXT,
    LAYER_OUTPUT_MAX
} EXYNOSLayerOutput;

typedef enum {
    LAYER_NONE = -3,
    LAYER_LOWER2 = -2,
    LAYER_LOWER1 = -1,
    LAYER_DEFAULT = 0,
    LAYER_UPPER = +1,
    LAYER_MAX = +2,
#ifdef LAYER_MANAGER
    FOR_LAYER_MNG = +3,
#endif
} EXYNOSLayerPos;

#define LAYER_DESTROYED         1
#define LAYER_SHOWN             2
#define LAYER_HIDDEN            3
/* To manage buffer */
#define LAYER_BUF_CHANGED       4       /* type_data: EXYNOSLayerBufInfo */
#define LAYER_VBLANK            5       /* type_data: EXYNOSLayerVblankDatePtr */

typedef struct _layerVblankDate {
    unsigned int frame;
    unsigned int tv_sec;
    unsigned int tv_usec;
    EXYNOSVideoBuf *vbuf;
} EXYNOSLayerVblankDateRec, *EXYNOSLayerVblankDatePtr;

typedef struct _EXYNOSLayer EXYNOSLayer, *EXYNOSLayerPtr;

typedef void (*NotifyFunc) (EXYNOSLayer * layer, int type, void *type_data,
                            void *user_data);

Bool exynosLayerSupport(ScrnInfoPtr pScrn, EXYNOSLayerOutput output,
                        EXYNOSLayerPos lpos, unsigned int id);

EXYNOSLayer *exynosLayerFind(EXYNOSLayerOutput output, EXYNOSLayerPos lpos);
EXYNOSLayer *exynosLayerFindByDraw(DrawablePtr);
void exynosLayerDestroyAll(void);
void exynosLayerShowAll(ScrnInfoPtr pScrn, EXYNOSLayerOutput output);

void exynosLayerAddNotifyFunc(EXYNOSLayer * layer, NotifyFunc func,
                              void *user_data);
void exynosLayerRemoveNotifyFunc(EXYNOSLayer * layer, NotifyFunc func);

EXYNOSLayer *exynosLayerCreate(ScrnInfoPtr pScrn, EXYNOSLayerOutput output,
                               EXYNOSLayerPos lpos);
EXYNOSLayer *exynosLayerRef(EXYNOSLayer * layer);
void exynosLayerUnref(EXYNOSLayer * layer);

Bool exynosLayerIsVisible(EXYNOSLayer * layer);
void exynosLayerShow(EXYNOSLayer * layer);
void exynosLayerHide(EXYNOSLayer * layer);
void exynosLayerFreezeUpdate(EXYNOSLayer * layer, Bool enable);
Bool exynosLayerIsNeedUpdate(EXYNOSLayer * layer);
void exynosLayerUpdate(EXYNOSLayer * layer);
void exynosLayerTurn(EXYNOSLayer * layer, Bool onoff, Bool user);
Bool exynosLayerTurnStatus(EXYNOSLayer * layer);

void exynosLayerEnableVBlank(EXYNOSLayer * layer, Bool enable);
Bool exynosLayerIsPanding(EXYNOSLayer * layer);
Bool exynosLayerSetOffset(EXYNOSLayer * layer, int x, int y);
void exynosLayerGetOffset(EXYNOSLayer * layer, int *x, int *y);

Bool exynosLayerSetPos(EXYNOSLayer * layer, EXYNOSLayerPos lpos);
EXYNOSLayerPos exynosLayerGetPos(EXYNOSLayer * layer);
Bool exynosLayerSwapPos(EXYNOSLayer * layer1, EXYNOSLayer * layer2);

Bool exynosLayerSetRect(EXYNOSLayer * layer, xRectangle *src, xRectangle *dst);
void exynosLayerGetRect(EXYNOSLayer * layer, xRectangle *src, xRectangle *dst);

int exynosLayerSetBuffer(EXYNOSLayer * layer, EXYNOSVideoBuf * vbuf);
EXYNOSVideoBuf *exynosLayerGetBuffer(EXYNOSLayer * layer);

DrawablePtr exynosLayerGetDraw(EXYNOSLayer * layer);
Bool exynosLayerSetDraw(EXYNOSLayer * layer, DrawablePtr pDraw);

EXYNOSVideoBuf *exynosLayerGetMatchBuf(EXYNOSLayer * layer, tbm_bo bo);

Bool exynosLayerIsUpdateDRI(EXYNOSLayer * layer);
void exynosLayerUpdateDRI(EXYNOSLayer * layer, Bool);

Bool exynosLayerSetAsDefault(EXYNOSLayer * layer);
EXYNOSLayerPtr exynosLayerGetDefault(xf86CrtcPtr pCrtc);

ScrnInfoPtr exynosLayerGetScrn(EXYNOSLayer * layer);

void exynosLayerVBlankEventHandler(unsigned int frame, unsigned int tv_sec,
                                   unsigned int tv_usec, void *event_data);
Bool exynosLayerExistNotifyFunc(EXYNOSLayer * layer, NotifyFunc func);

void exynosLayerDestroy(EXYNOSLayer * layer);
void exynosLayerClearQueue(EXYNOSLayer * layer);
Bool exynosLayerSetOutput(EXYNOSLayer * layer, EXYNOSLayerOutput output);
#endif                          /* __SEC_LAYER_H__ */
