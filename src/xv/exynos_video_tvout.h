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
#include "exynos_layer.h"
#include "exynos_converter.h"
#include "exynos_video_types.h"

typedef struct _EXYNOSVideoTv EXYNOSVideoTv;

EXYNOSVideoTv *exynosVideoTvConnect(ScrnInfoPtr pScrn, unsigned int id,
                                    EXYNOSLayerPos lpos);
void exynosVideoTvDisconnect(EXYNOSVideoTv * tv);

Bool exynosVideoTvSetBuffer(EXYNOSVideoTv * tv, EXYNOSVideoBuf ** vbufs,
                            int bufnum);

EXYNOSLayerPos exynosVideoTvGetPos(EXYNOSVideoTv * tv);
EXYNOSLayer *exynosVideoTvGetLayer(EXYNOSVideoTv * tv);

void exynosVideoTvSetSize(EXYNOSVideoTv * tv, int width, int height);
int exynosVideoTvPutImage(EXYNOSVideoTv * tv, EXYNOSVideoBuf * vbuf,
                          xRectangle *dst, int csc_range);

EXYNOSCvt *exynosVideoTvGetConverter(EXYNOSVideoTv * tv);
void exynosVideoTvSetConvertFormat(EXYNOSVideoTv * tv, unsigned int convert_id);
Bool exynosVideoTvResizeOutput(EXYNOSVideoTv * tv, xRectanglePtr src,
                               xRectanglePtr dst);
Bool exynosVideoTvCanDirectDrawing(EXYNOSVideoTv * tv, int src_w, int src_h,
                                   int dst_w, int dst_h);
Bool exynosVideoTvReCreateConverter(EXYNOSVideoTv * tv);
Bool exynosVideoTvSetAttributes(EXYNOSVideoTv * tv, int rotate, int hflip,
                                int vflip);

#endif                          /* __SEC_VIDEO_TVOUT_H__ */
