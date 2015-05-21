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

#include "sec_video_types.h"
#include "sec_video_fourcc.h"
#include <xf86Crtc.h>

typedef enum
{
    LAYER_OUTPUT_LCD,
    LAYER_OUTPUT_EXT,
    LAYER_OUTPUT_MAX
} SECLayerOutput;

typedef enum
{
    LAYER_NONE      = -3,
    LAYER_LOWER2    = -2,
    LAYER_LOWER1    = -1,
    LAYER_DEFAULT   =  0,
    LAYER_UPPER     = +1,
    LAYER_MAX       = +2,
#ifdef LAYER_MANAGER
    FOR_LAYER_MNG   = +3,
#endif
} SECLayerPos;

#define LAYER_DESTROYED         1
#define LAYER_SHOWN             2
#define LAYER_HIDDEN            3
/* To manage buffer */
#define LAYER_BUF_CHANGED       4  /* type_data: SECLayerBufInfo */
#define LAYER_VBLANK            5  /* type_data: SECLayerVblankDatePtr */

typedef struct _layerVblankDate
{
    unsigned int frame;
    unsigned int tv_sec;
    unsigned int tv_usec;
    SECVideoBuf * vbuf;
} SECLayerVblankDateRec, * SECLayerVblankDatePtr;

typedef struct _SECLayer SECLayer, *SECLayerPtr;

typedef void (*NotifyFunc) (SECLayer *layer, int type, void *type_data, void *user_data);

Bool        secLayerSupport     (ScrnInfoPtr pScrn, SECLayerOutput output,
                                 SECLayerPos lpos, unsigned int id);

SECLayer*   secLayerFind        (SECLayerOutput output, SECLayerPos lpos);
SECLayer*   secLayerFindByDraw  (DrawablePtr);
void        secLayerDestroyAll  (void);
void        secLayerShowAll     (ScrnInfoPtr pScrn, SECLayerOutput output);

void        secLayerAddNotifyFunc    (SECLayer *layer, NotifyFunc func, void *user_data);
void        secLayerRemoveNotifyFunc (SECLayer *layer, NotifyFunc func);

SECLayer*   secLayerCreate    (ScrnInfoPtr pScrn, SECLayerOutput output, SECLayerPos lpos);
SECLayer*   secLayerRef       (SECLayer *layer);
void        secLayerUnref     (SECLayer *layer);

Bool        secLayerIsVisible (SECLayer *layer);
void        secLayerShow      (SECLayer *layer);
void        secLayerHide      (SECLayer *layer);
void        secLayerFreezeUpdate (SECLayer *layer, Bool enable);
Bool        secLayerIsNeedUpdate (SECLayer *layer);
void        secLayerUpdate    (SECLayer *layer);
void        secLayerTurn      (SECLayer *layer, Bool onoff, Bool user);
Bool        secLayerTurnStatus (SECLayer *layer);

void        secLayerEnableVBlank (SECLayer *layer, Bool enable);
Bool        secLayerIsPanding (SECLayer *layer);
Bool        secLayerSetOffset (SECLayer *layer, int x, int y);
void        secLayerGetOffset (SECLayer *layer, int *x, int *y);

Bool        secLayerSetPos    (SECLayer *layer, SECLayerPos lpos);
SECLayerPos secLayerGetPos    (SECLayer *layer);
Bool        secLayerSwapPos   (SECLayer *layer1, SECLayer *layer2);

Bool        secLayerSetRect   (SECLayer *layer, xRectangle *src, xRectangle *dst);
void        secLayerGetRect   (SECLayer *layer, xRectangle *src, xRectangle *dst);

int          secLayerSetBuffer (SECLayer *layer, SECVideoBuf *vbuf);
SECVideoBuf* secLayerGetBuffer (SECLayer *layer);

DrawablePtr secLayerGetDraw   (SECLayer *layer);
Bool        secLayerSetDraw   (SECLayer *layer, DrawablePtr pDraw);

SECVideoBuf* secLayerGetMatchBuf(SECLayer *layer, tbm_bo bo);

Bool        secLayerIsUpdateDRI (SECLayer *layer);
void        secLayerUpdateDRI   (SECLayer *layer, Bool );

Bool        secLayerSetAsDefault(SECLayer *layer);
SECLayerPtr secLayerGetDefault (xf86CrtcPtr pCrtc);

ScrnInfoPtr secLayerGetScrn   (SECLayer *layer);

void        secLayerVBlankEventHandler (unsigned int frame, unsigned int tv_sec,
                                        unsigned int tv_usec, void *event_data);
Bool        secLayerExistNotifyFunc (SECLayer* layer, NotifyFunc func);

void        secLayerDestroy(SECLayer *layer);
void        secLayerClearQueue (SECLayer *layer);
Bool        secLayerSetOutput (SECLayer *layer, SECLayerOutput output);
#endif /* __SEC_LAYER_H__ */
