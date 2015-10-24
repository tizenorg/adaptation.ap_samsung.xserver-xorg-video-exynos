/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>

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

#ifndef __SEC_OUTPUT_H__
#define __SEC_OUTPUT_H__

#include "exynos_display.h"

#ifndef RR_Rotate_All
#define RR_Rotate_All  (RR_Rotate_0|RR_Rotate_90|RR_Rotate_180|RR_Rotate_270)
#endif                          //RR_Rotate_All
#ifndef RR_Reflect_All
#define RR_Reflect_All (RR_Reflect_X|RR_Reflect_Y)
#endif                          //RR_Reflect_All

typedef struct _exynosOutputPriv {
    EXYNOSModePtr pExynosMode;
    int output_id;
    drmModeConnectorPtr mode_output;
    drmModeEncoderPtr mode_encoder;
    int num_props;
    EXYNOSPropertyPtr props;
    void *private_data;

    Bool isLcdOff;
    int dpms_mode;

    int disp_mode;

    xf86OutputPtr pOutput;
    Bool is_dummy;
    struct xorg_list link;
} EXYNOSOutputPrivRec, *EXYNOSOutputPrivPtr;

#if 0
Bool exynosOutputDummyInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode,
                           Bool late);
#endif
void exynosOutputInit(ScrnInfoPtr pScrn, EXYNOSModePtr pExynosMode, int num);
int exynosOutputDpmsStatus(xf86OutputPtr pOutput);
void exynosOutputDpmsSet(xf86OutputPtr pOutput, int mode);

Bool exynosOutputDrmUpdate(ScrnInfoPtr pScrn);

EXYNOSOutputPrivPtr exynosOutputGetPrivateForConnType(ScrnInfoPtr pScrn,
                                                      int connect_type);

#endif                          /* __SEC_OUTPUT_H__ */
