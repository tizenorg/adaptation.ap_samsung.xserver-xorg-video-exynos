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

#ifndef __SEC_UTIL_H__
#define __SEC_UTIL_H__

#include <fbdevhw.h>
#include <pixman.h>
#include <list.h>
#include <xdbg.h>
#include "fimg2d.h"

#include "sec.h"
#include "property.h"
#include "sec_display.h"
#include "sec_video_types.h"
#include "sec_xberc.h"

#define MFB     XDBG_M('F','B',0,0)
#define MDISP   XDBG_M('D','I','S','P')
#define MLYR    XDBG_M('L','Y','R',0)
#define MPLN    XDBG_M('P','L','N',0)
#define MSEC    XDBG_M('S','E','C',0)
#define MEXA    XDBG_M('E','X','A',0)
#define MEXAS   XDBG_M('E','X','A','S')
#define MEVT    XDBG_M('E','V','T',0)
#define MDRI2   XDBG_M('D','R','I','2')
#define MCRS    XDBG_M('C','R','S',0)
#define MFLIP   XDBG_M('F','L','I','P')
#define MDPMS   XDBG_M('D','P','M','S')
#define MVDO    XDBG_M('V','D','O',0)
#define MDA     XDBG_M('D','A',0,0)
#define MTVO    XDBG_M('T','V','O',0)
#define MWB     XDBG_M('W','B',0,0)
#define MVA     XDBG_M('V','A',0,0)
#define MPROP   XDBG_M('P','R','O','P')
#define MXBRC   XDBG_M('X','B','R','C')
#define MVBUF   XDBG_M('V','B','U','F')
#define MDRM    XDBG_M('D','R','M',0)
#define MACCE   XDBG_M('A','C','C','E')
#define MCVT    XDBG_M('C','V','T',0)
#define MEXAH   XDBG_M('E','X','A','H')
#define MG2D    XDBG_M('G','2','D',0)

#define _XID(win)   ((unsigned int)(((WindowPtr)win)->drawable.id))

#define UTIL_DUMP_OK             0
#define UTIL_DUMP_ERR_OPENFILE   1
#define UTIL_DUMP_ERR_SHMATTACH  2
#define UTIL_DUMP_ERR_SEGSIZE    3
#define UTIL_DUMP_ERR_CONFIG     4
#define UTIL_DUMP_ERR_INTERNAL   5

#define rgnSAME	3

#define DUMP_DIR "/tmp/xdump"

int secUtilDumpBmp (const char * file, const void * data, int width, int height);
int secUtilDumpRaw (const char * file, const void * data, int size);
int secUtilDumpShm (int shmid, const void * data, int width, int height);
int secUtilDumpPixmap (const char * file, PixmapPtr pPixmap);

void* secUtilPrepareDump (ScrnInfoPtr pScrn, int bo_size, int buf_cnt);
void  secUtilDoDumpRaws  (void *dump, tbm_bo *bo, int *size, int bo_cnt, const char *file);
void  secUtilDoDumpBmps  (void *d, tbm_bo bo, int w, int h, xRectangle *crop, const char *file);
void  secUtilDoDumpPixmaps (void *d, PixmapPtr pPixmap, const char *file);
void  secUtilDoDumpVBuf  (void *d, SECVideoBuf *vbuf, const char *file);
void  secUtilFlushDump   (void *dump);
void  secUtilFinishDump  (void *dump);

int secUtilDegreeToRotate (int degree);
int secUtilRotateToDegree (int rotate);
int secUtilRotateAdd (int rot_a, int rot_b);

void secUtilCacheFlush (ScrnInfoPtr scrn);

void*   secUtilCopyImage     (int width, int height,
                              char *s, int s_size_w, int s_size_h,
                              int *s_pitches, int *s_offsets, int *s_lengths,
                              char *d, int d_size_w, int d_size_h,
                              int *d_pitches, int *d_offsets, int *d_lengths,
                              int channel, int h_sampling, int v_sampling);

void secUtilRotateArea (int *width, int *height, xRectangle *rect, int degree);
void secUtilRotateRect2 (int width, int height, xRectangle *rect, int degree, const char *func);
#define secUtilRotateRect(w,h,r,d) secUtilRotateRect2(w,h,r,d,__FUNCTION__)
void secUtilRotateRegion (int width, int height, RegionPtr region, int degree);

void    secUtilAlignRect (int src_w, int src_h, int dst_w, int dst_h, xRectangle *fit, Bool hw);
void    secUtilScaleRect (int src_w, int src_h, int dst_w, int dst_h, xRectangle *scale);

const PropertyPtr secUtilGetWindowProperty (WindowPtr pWin, const char* prop_name);

int secUtilBoxInBox (BoxPtr base, BoxPtr box);
int secUtilBoxArea(BoxPtr pBox);
int secUtilBoxIntersect(BoxPtr pDstBox, BoxPtr pBox1, BoxPtr pBox2);
void secUtilBoxMove(BoxPtr pBox, int dx, int dy);

Bool secUtilRectIntersect (xRectanglePtr pDest, xRectanglePtr pRect1, xRectanglePtr pRect2);

void secUtilSaveImage (pixman_image_t *image, char *path);
Bool secUtilConvertImage (pixman_op_t op, uchar *srcbuf, uchar *dstbuf,
                          pixman_format_code_t src_format, pixman_format_code_t dst_format,
                          int sw, int sh, xRectangle *sr,
                          int dw, int dh, xRectangle *dr,
                          RegionPtr dst_clip_region,
                          int rotate, int hflip, int vflip);
void secUtilConvertBos (ScrnInfoPtr pScrn,
                        tbm_bo src_bo, int sw, int sh, xRectangle *sr, int sstride,
                        tbm_bo dst_bo, int dw, int dh, xRectangle *dr, int dstride,
                        Bool composite, int rotate);

void secUtilFreeHandle        (ScrnInfoPtr scrn, unsigned int handle);
Bool secUtilConvertPhyaddress (ScrnInfoPtr scrn, unsigned int phy_addr, int size, unsigned int *handle);
Bool secUtilConvertHandle     (ScrnInfoPtr scrn, unsigned int handle, unsigned int *phy_addr, int *size);

typedef void (*DestroyDataFunc) (void *func_data, void *key_data);

void* secUtilListAdd     (void *list, void *key, void *key_data);
void* secUtilListRemove  (void *list, void *key);
void* secUtilListGetData (void *list, void *key);
Bool  secUtilListIsEmpty (void *list);
void  secUtilListDestroyData (void *list, DestroyDataFunc func, void *func_data);
void  secUtilListDestroy (void *list);

Bool  secUtilSetDrmProperty (SECModePtr pSecMode, unsigned int obj_id, unsigned int obj_type,
                             const char *prop_name, unsigned int value);

Bool  secUtilEnsureExternalCrtc (ScrnInfoPtr scrn);

G2dColorMode  secUtilGetG2dFormat (unsigned int id);
unsigned int  secUtilGetDrmFormat (unsigned int id);
SECFormatType secUtilGetColorType (unsigned int id);

SECVideoBuf* _secUtilAllocVideoBuffer  (ScrnInfoPtr scrn, int id, int width, int height,
                                        Bool scanout, Bool reset, Bool secure, const char *func);
SECVideoBuf* _secUtilCreateVideoBuffer (ScrnInfoPtr scrn, int id, int width, int height,
                                        Bool secure, const char *func);
SECVideoBuf* secUtilVideoBufferRef     (SECVideoBuf *vbuf);
void         _secUtilVideoBufferUnref  (SECVideoBuf *vbuf, const char *func);
void         _secUtilFreeVideoBuffer   (SECVideoBuf *vbuf, const char *func);
void         secUtilClearVideoBuffer   (SECVideoBuf *vbuf);
Bool         _secUtilIsVbufValid       (SECVideoBuf *vbuf, const char *func);

typedef void (*FreeVideoBufFunc) (SECVideoBuf *vbuf, void *data);
void         secUtilAddFreeVideoBufferFunc     (SECVideoBuf *vbuf, FreeVideoBufFunc func, void *data);
void         secUtilRemoveFreeVideoBufferFunc  (SECVideoBuf *vbuf, FreeVideoBufFunc func, void *data);

#define secUtilAllocVideoBuffer(s,i,w,h,c,r,d)  _secUtilAllocVideoBuffer(s,i,w,h,c,r,d,__FUNCTION__)
#define secUtilCreateVideoBuffer(s,i,w,h,d)     _secUtilCreateVideoBuffer(s,i,w,h,d,__FUNCTION__)
#define secUtilVideoBufferUnref(v)  _secUtilVideoBufferUnref(v,__FUNCTION__)
#define secUtilFreeVideoBuffer(v)   _secUtilFreeVideoBuffer(v,__FUNCTION__)
#define secUtilIsVbufValid(v)       _secUtilIsVbufValid(v,__FUNCTION__)
#define VBUF_IS_VALID(v)            secUtilIsVbufValid(v)
#define VSTMAP(v)            ((v)?(v)->stamp:0)
#define VBUF_IS_CONVERTING(v)       (!xorg_list_is_empty (&((v)->convert_info)))

/* for debug */
char*  secUtilDumpVideoBuffer (char *reply, int *len);

#define list_rev_for_each_entry_safe(pos, tmp, head, member) \
    for (pos = __container_of((head)->prev, pos, member), tmp = __container_of(pos->member.prev, pos, member);\
         &pos->member != (head);\
         pos = tmp, tmp = __container_of(pos->member.prev, tmp, member))

#endif /* __SEC_UTIL_H__ */
