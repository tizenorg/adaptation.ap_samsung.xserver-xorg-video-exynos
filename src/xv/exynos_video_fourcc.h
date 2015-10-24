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

#ifndef __SEC_VIDEO_FOURCC_H__
#define __SEC_VIDEO_FOURCC_H__

#include <fourcc.h>
#include <drm_fourcc.h>

#define C(b,m)              (char)(((b) >> (m)) & 0xFF)
#define B(c,s)              ((((unsigned int)(c)) & 0xff) << (s))
#define FOURCC(a,b,c,d)     (B(d,24) | B(c,16) | B(b,8) | B(a,0))
#define FOURCC_STR(id)      C(id,0), C(id,8), C(id,16), C(id,24)
#define FOURCC_ID(str)      FOURCC(((char*)str)[0],((char*)str)[1],((char*)str)[2],((char*)str)[3])
#define IS_ZEROCOPY(id)     ((C(id,0) == 'S') || id == FOURCC_ITLV)
#define IS_RGB(id)           (id == FOURCC_RGB565 || id == FOURCC_RGB32 || \
                              id == FOURCC_SR16 || id == FOURCC_SR32 || \
                              id == FOURCC_RGB24)
#define IS_YUV(id)           (id == FOURCC_Y444 || id == FOURCC_S420    || \
                              id == FOURCC_SUYV || id == FOURCC_SYVY)

/* Specific format for S.LSI
 * 2x2 subsampled Cr:Cb plane 64x32 macroblocks
 */
#define DRM_FORMAT_NV12MT       fourcc_code('T', 'M', '1', '2')

/* http://www.fourcc.org/yuv.php
 * http://en.wikipedia.org/wiki/YUV
 */
#define FOURCC_RGB565   FOURCC('R','G','B','P')
#define XVIMAGE_RGB565 \
   { \
    FOURCC_RGB565, \
    XvRGB, \
    LSBFirst, \
    {'R','G','B','P', \
        0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    16, \
    XvPacked, \
    1, \
    16, 0x0000F800, 0x000007E0, 0x0000001F, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, \
    {'R','G','B',0, \
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_SR16   FOURCC('S','R','1','6')
#define XVIMAGE_SR16 \
   { \
    FOURCC_SR16, \
    XvRGB, \
    LSBFirst, \
    {'S','R','1','6', \
        0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    16, \
    XvPacked, \
    1, \
    16, 0x0000F800, 0x000007E0, 0x0000001F, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, \
    {'R','G','B',0, \
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_RGB24    FOURCC('R','G','B','3')
#define XVIMAGE_RGB24 \
   { \
    FOURCC_RGB24, \
    XvRGB, \
    LSBFirst, \
    {'R','G','B','3', \
        0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    24, \
    XvPacked, \
    1, \
    24, 0x00FF0000, 0x0000FF00, 0x000000FF, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, \
    {'R','G','B',0, \
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_RGB32    FOURCC('R','G','B','4')
#define XVIMAGE_RGB32 \
   { \
    FOURCC_RGB32, \
    XvRGB, \
    LSBFirst, \
    {'R','G','B','4', \
        0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    32, \
    XvPacked, \
    1, \
    24, 0x00FF0000, 0x0000FF00, 0x000000FF, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, \
    {'X','R','G','B', \
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_SR32    FOURCC('S','R','3','2')
#define XVIMAGE_SR32 \
   { \
    FOURCC_SR32, \
    XvRGB, \
    LSBFirst, \
    {'S','R','3','2', \
        0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    32, \
    XvPacked, \
    1, \
    24, 0x00FF0000, 0x0000FF00, 0x000000FF, \
    0, 0, 0, 0, 0, 0, 0, 0, 0, \
    {'X','R','G','B', \
        0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_ST12     FOURCC('S','T','1','2')
#define XVIMAGE_ST12 \
   { \
    FOURCC_ST12, \
    XvYUV, \
    LSBFirst, \
    {'S','T','1','2', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','U','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_SN12     FOURCC('S','N','1','2')
#define XVIMAGE_SN12 \
   { \
    FOURCC_SN12, \
    XvYUV, \
    LSBFirst, \
    {'S','N','1','2', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','U','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_NV12     FOURCC('N','V','1','2')
#define XVIMAGE_NV12 \
   { \
    FOURCC_NV12, \
    XvYUV, \
    LSBFirst, \
    {'N','V','1','2', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','U','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_SN21     FOURCC('S','N','2','1')
#define XVIMAGE_SN21 \
   { \
    FOURCC_SN21, \
    XvYUV, \
    LSBFirst, \
    {'S','N','2','1', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','V','U', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_NV21     FOURCC('N','V','2','1')
#define XVIMAGE_NV21 \
   { \
    FOURCC_NV21, \
    XvYUV, \
    LSBFirst, \
    {'N','V','2','1', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','V','U', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_S420     FOURCC('S','4','2','0')
#define XVIMAGE_S420 \
   { \
    FOURCC_S420, \
        XvYUV, \
    LSBFirst, \
    {'S','4','2','0', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    3, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','U','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }
#define FOURCC_SUYV     FOURCC('S','U','Y','V')
#define XVIMAGE_SUYV \
   { \
    FOURCC_SUYV, \
        XvYUV, \
    LSBFirst, \
    {'S','U','Y','V', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    16, \
    XvPacked, \
    1, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 1, 1, \
    {'Y','U','Y','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_SYVY   FOURCC('S','Y','V','Y')
#define XVIMAGE_SYVY \
   { \
    FOURCC_SYVY, \
        XvYUV, \
    LSBFirst, \
    {'S','Y','V','Y', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    16, \
    XvPacked, \
    1, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 1, 1, \
    {'U','Y','V','Y', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_ITLV   FOURCC('I','T','L','V')
#define XVIMAGE_ITLV \
   { \
    FOURCC_ITLV, \
        XvYUV, \
    LSBFirst, \
    {'I','T','L','V', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    16, \
    XvPacked, \
    1, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 1, 1, \
    {'U','Y','V','Y', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define FOURCC_Y444  FOURCC('Y','4','4','4')

#endif                          // __SEC_VIDEO_FOURCC_H__
