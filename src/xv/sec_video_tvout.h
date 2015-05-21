/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

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

#ifndef __SEC_VIDEO_TVOUT_H__
#define __SEC_VIDEO_TVOUT_H__

#include <xf86str.h>
#include <X11/Xdefs.h>
#include "sec_layer.h"
#include "sec_converter.h"
#include "sec_video_types.h"

typedef struct _SECVideoTv SECVideoTv;

SECVideoTv* secVideoTvConnect     (ScrnInfoPtr pScrn, unsigned int id, SECLayerPos lpos);
void        secVideoTvDisconnect  (SECVideoTv *tv);

Bool        secVideoTvSetBuffer   (SECVideoTv* tv, SECVideoBuf **vbufs, int bufnum);

SECLayerPos secVideoTvGetPos      (SECVideoTv *tv);
SECLayer*   secVideoTvGetLayer    (SECVideoTv *tv);

void        secVideoTvSetSize     (SECVideoTv *tv, int width, int height);
int         secVideoTvPutImage    (SECVideoTv *tv, SECVideoBuf *vbuf, xRectangle *dst, int csc_range);

SECCvt*     secVideoTvGetConverter (SECVideoTv *tv);
void        secVideoTvSetConvertFormat (SECVideoTv *tv, unsigned int convert_id);
Bool        secVideoTvResizeOutput (SECVideoTv* tv, xRectanglePtr src, xRectanglePtr dst);
Bool        secVideoTvCanDirectDrawing (SECVideoTv* tv, int src_w, int src_h, int dst_w, int dst_h);
Bool        secVideoTvReCreateConverter (SECVideoTv* tv);
Bool        secVideoTvSetAttributes(SECVideoTv* tv, int rotate, int hflip, int vflip);

#endif /* __SEC_VIDEO_TVOUT_H__ */
