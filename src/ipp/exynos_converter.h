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

#ifndef __SEC_CONVERTER_H__
#define __SEC_CONVERTER_H__

#include <fbdevhw.h>
#include <exynos/exynos_drm.h>

typedef struct _EXYNOSCvt EXYNOSCvt;

typedef enum {
    CVT_OP_M2M,
    CVT_OP_OUTPUT,
    CVT_OP_MAX,
} EXYNOSCvtOp;

typedef enum {
    CVT_TYPE_SRC,
    CVT_TYPE_DST,
    CVT_TYPE_MAX,
} EXYNOSCvtType;

typedef struct _EXYNOSCvtProp {
    unsigned int id;

    int width;
    int height;
    xRectangle crop;

    int degree;
    Bool vflip;
    Bool hflip;
    Bool exynosure;
    int csc_range;
} EXYNOSCvtProp;

typedef struct _EXYNOSCvtFmt {
    unsigned int id;

} EXYNOSCvtFmt;

Bool exynosCvtSupportFormat(EXYNOSCvtOp op, int id);
Bool exynosCvtEnsureSize(EXYNOSCvtProp * src, EXYNOSCvtProp * dst);

EXYNOSCvt *exynosCvtCreate(ScrnInfoPtr pScrn, EXYNOSCvtOp op);
void exynosCvtDestroy(EXYNOSCvt * cvt);
EXYNOSCvtOp exynosCvtGetOp(EXYNOSCvt * cvt);
Bool exynosCvtSetProperpty(EXYNOSCvt * cvt, EXYNOSCvtProp * src,
                           EXYNOSCvtProp * dst);
void exynosCvtGetProperpty(EXYNOSCvt * cvt, EXYNOSCvtProp * src,
                           EXYNOSCvtProp * dst);
Bool exynosCvtConvert(EXYNOSCvt * cvt, EXYNOSVideoBuf * src,
                      EXYNOSVideoBuf * dst);

typedef void (*CvtFunc) (EXYNOSCvt * cvt, EXYNOSVideoBuf * src,
                         EXYNOSVideoBuf * dst, void *cvt_data, Bool error);
Bool exynosCvtAddCallback(EXYNOSCvt * cvt, CvtFunc func, void *data);
void exynosCvtRemoveCallback(EXYNOSCvt * cvt, CvtFunc func, void *data);

void exynosCvtHandleIppEvent(int fd, unsigned int *buf_idx, void *data,
                             Bool error);
Bool exynosCvtPause(EXYNOSCvt * cvt);
uintptr_t exynosCvtGetStamp(EXYNOSCvt * cvt);

#endif                          /* __SEC_CONVERTER_H__ */
