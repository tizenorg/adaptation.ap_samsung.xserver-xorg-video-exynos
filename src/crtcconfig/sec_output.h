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

#include "sec_display.h"

typedef struct _secOutputPriv
{
    SECModePtr pSecMode;
    int output_id;
    drmModeConnectorPtr mode_output;
    drmModeEncoderPtr mode_encoder;
    int num_props;
    SECPropertyPtr props;
    void *private_data;

    Bool isLcdOff;
    int dpms_mode;

    int disp_mode;

    xf86OutputPtr pOutput;
    struct xorg_list link;
} SECOutputPrivRec, *SECOutputPrivPtr;


void    secOutputInit       (ScrnInfoPtr pScrn, SECModePtr pSecMode, int num);
int     secOutputDpmsStatus (xf86OutputPtr pOutput);
void    secOutputDpmsSet    (xf86OutputPtr pOutput, int mode);

Bool    secOutputDrmUpdate  (ScrnInfoPtr pScrn);

SECOutputPrivPtr secOutputGetPrivateForConnType (ScrnInfoPtr pScrn, int connect_type);

#endif /* __SEC_OUTPUT_H__ */

