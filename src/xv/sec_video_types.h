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

#ifndef __SEC_V4L2_TYPES_H__
#define __SEC_V4L2_TYPES_H__

#include <sys/types.h>
#include <fbdevhw.h>
#include <X11/Xdefs.h>
#include <tbm_bufmgr.h>
#include <exynos_drm.h>

/* securezone memory */
#define TZMEM_IOC_GET_TZMEM 0xC00C5402
struct tzmem_get_region
{
   const char   *key;
   unsigned int size;
   int          fd;
};

/************************************************************
 * DEFINITION
 ************************************************************/
#ifndef CLEAR
#define CLEAR(x) memset(&(x), 0, sizeof (x))
#endif

#ifndef MAX
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef SWAP
#define SWAP(a, b)  ({int t; t = a; a = b; b = t;})
#endif

#ifndef ROUNDUP
#define ROUNDUP(x)  (ceil (floor ((float)(height) / 4)))
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef TRUE
#define TRUE 1
#endif

enum fimc_overlay_mode {
    FIMC_OVLY_NOT_FIXED = 0,
    FIMC_OVLY_FIFO,
    FIMC_OVLY_DMA_AUTO,
    FIMC_OVLY_DMA_MANUAL,
    FIMC_OVLY_NONE_SINGLE_BUF,
    FIMC_OVLY_NONE_MULTI_BUF,
};

#define ALIGN_TO_16B(x)    ((((x) + (1 <<  4) - 1) >>  4) <<  4)
#define ALIGN_TO_32B(x)    ((((x) + (1 <<  5) - 1) >>  5) <<  5)
#define ALIGN_TO_128B(x)   ((((x) + (1 <<  7) - 1) >>  7) <<  7)
#define ALIGN_TO_2KB(x)    ((((x) + (1 << 11) - 1) >> 11) << 11)
#define ALIGN_TO_8KB(x)    ((((x) + (1 << 13) - 1) >> 13) << 13)
#define ALIGN_TO_64KB(x)   ((((x) + (1 << 16) - 1) >> 16) << 16)

#define PLANAR_CNT    EXYNOS_DRM_PLANAR_MAX

/************************************************************
 * TYPE DEFINITION
 ************************************************************/

#ifndef uchar
typedef unsigned char uchar;
#endif

typedef enum
{
    TYPE_NONE,
    TYPE_RGB,
    TYPE_YUV444,
    TYPE_YUV422,
    TYPE_YUV420,
} SECFormatType;

typedef struct _SECFormatTable
{
    unsigned int  id;
    unsigned int  drmfmt;
    SECFormatType type;
} SECFormatTable;

typedef struct _ConvertInfo
{
    void *cvt;
    struct xorg_list link;
} ConvertInfo;

typedef struct _SECVideoBuf
{
    ScrnInfoPtr pScrn;
    int     id;

    int     width;
    int     height;
    xRectangle crop;

    int     pitches[PLANAR_CNT];
    int     offsets[PLANAR_CNT];
    int     lengths[PLANAR_CNT];
    int     size;

    tbm_bo   bo[PLANAR_CNT];
    unsigned int keys[PLANAR_CNT];
    unsigned int phy_addrs[PLANAR_CNT];
    unsigned int handles[PLANAR_CNT];

    struct xorg_list convert_info;
    Bool    showing;         /* now showing or now waiting to show. */
    Bool    dirty;
    Bool    need_reset;

    unsigned int fb_id;      /* fb_id of vbuf */

    struct xorg_list free_funcs;

    Bool    secure;
    int     csc_range;

    struct xorg_list valid_link;   /* to check valid */
    CARD32 stamp;
    unsigned int ref_cnt;
    char   *func;
    int     flags;
    Bool    scanout;

    CARD32 put_time;
} SECVideoBuf;

#endif
