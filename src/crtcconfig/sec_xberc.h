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

#ifndef __SEC_XBERC_H__
#define __SEC_XBERC_H__

#define XBERC

#define XBERC_DUMP_MODE_DRAWABLE    0x1
#define XBERC_DUMP_MODE_FB          0x2
#define XBERC_DUMP_MODE_IA          0x10
#define XBERC_DUMP_MODE_CA          0x20
#define XBERC_DUMP_MODE_EA          0x40

#define XBERC_XVPERF_MODE_IA        0x1
#define XBERC_XVPERF_MODE_CA        0x2
#define XBERC_XVPERF_MODE_CVT       0x10
#define XBERC_XVPERF_MODE_WB        0x20
#define XBERC_XVPERF_MODE_ACCESS    0x40

int    secXbercSetProperty (xf86OutputPtr output, Atom property, RRPropertyValuePtr value);

#endif /* __SEC_XBERC_H__ */

