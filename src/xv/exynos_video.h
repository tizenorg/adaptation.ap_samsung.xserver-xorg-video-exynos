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
#ifndef EXYNOS_VIDEO_H
#define EXYNOS_VIDEO_H

#define ADAPTOR_NUM  4

enum {
    OUTPUT_MODE_DEFAULT,
    OUTPUT_MODE_TVOUT,
    OUTPUT_MODE_EXT_ONLY,
};

typedef struct {
#ifdef XV
    XF86VideoAdaptorPtr pAdaptor[ADAPTOR_NUM];
#endif

    int video_punch;
    int video_fps;
    int video_sync;
    int video_fimc;
    int video_output;

    int video_offset_x;
    int video_offset_y;

    /* extension */
    int tvout_in_use;

    Bool no_retbuf;

    int screen_rotate_degree;

    /* for configure notify */
    RestackWindowProcPtr RestackWindow;

} EXYNOSVideoPriv, *EXYNOSVideoPrivPtr;

Bool exynosVideoInit(ScreenPtr pScreen);
void exynosVideoFini(ScreenPtr pScreen);

void exynosVideoDpms(ScrnInfoPtr pScrn, Bool on);
void exynosVideoScreenRotate(ScrnInfoPtr pScrn, int degree);
void exynosVideoSwapLayers(ScreenPtr pScreen);

int exynosVideoQueryImageAttrs(ScrnInfoPtr pScrn,
                               int id,
                               int *w,
                               int *h, int *pitches, int *offsets,
                               int *lengths);

Bool exynosVideoIsSecureMode(ScrnInfoPtr pScrn);

#endif                          // EXYNOS_VIDEO_H
