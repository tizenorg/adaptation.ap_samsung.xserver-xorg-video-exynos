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

#ifndef __SEC_DRM_IPP_H__
#define __SEC_DRM_IPP_H__

#include <fbdevhw.h>
#include <exynos_drm.h>

/* A drmfmt list newly allocated. should be freed. */
unsigned int* secDrmIppGetFormatList (int *num);

int     secDrmIppSetProperty   (ScrnInfoPtr pScrn, struct drm_exynos_ipp_property *property);
Bool    secDrmIppQueueBuf      (ScrnInfoPtr pScrn, struct drm_exynos_ipp_queue_buf *buf);
Bool    secDrmIppCmdCtrl       (ScrnInfoPtr pScrn, struct drm_exynos_ipp_cmd_ctrl *ctrl);

#endif  /* __SEC_DRM_IPP_H__ */