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
#include "xf86.h"
#include "exynos.h"
#include "property.h"
#include "exynos_display.h"
#include "exynos_video_types.h"
#include "exynos_xberc.h"

#define MFB     XDBG_M('F','B',0,0)
#define MDISP   XDBG_M('D','I','S','P')
#define MLYR    XDBG_M('L','Y','R',0)
#define MPLN    XDBG_M('P','L','N',0)
#define MSEC    XDBG_M('S','E','C',0)
#define MEXA    XDBG_M('E','X','A',0)
#define MEXAS   XDBG_M('E','X','A','S')
#define MEVT    XDBG_M('E','V','T',0)
#define MDRI2   XDBG_M('D','R','I','2')
#define MDRI3   XDBG_M('D','R','I','3')
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
#define MDOUT   XDBG_M('D','O','T',0)
#define MCLON   XDBG_M('C','L','O','N')
#define MHWC    XDBG_M('H','W','C',0)
#define MLYRM   XDBG_M('L','Y','R','M')
#define MHWA    XDBG_M('H','W','A',0)

#define _XID(win)   ((unsigned int)(((WindowPtr)win)->drawable.id))

#define UTIL_DUMP_OK             0
#define UTIL_DUMP_ERR_OPENFILE   1
#define UTIL_DUMP_ERR_SHMATTACH  2
#define UTIL_DUMP_ERR_SEGSIZE    3
#define UTIL_DUMP_ERR_CONFIG     4
#define UTIL_DUMP_ERR_INTERNAL   5
#define UTIL_DUMP_ERR_PNG        6

#define rgnSAME	3

#define DUMP_DIR "/tmp/xdump"

#ifdef LONG64
#define PRIXID	"u"
#else
#define PRIXID	"lu"
#endif

#define EARLY_ERROR_MSG(ARG...) do { xf86ErrorFVerb ( 0, ##ARG); } while(0)

typedef union {
    void *ptr;
    uint32_t u32;
    uint64_t u64;
} uniType;

//int exynosUtilDumpBmp (const char * file, const void * data, int width, int height);
int exynosUtilDumpRaw(const char *file, const void *data, int size);
int exynosUtilDumpShm(int shmid, const void *data, int width, int height);
int exynosUtilDumpPixmap(const char *file, PixmapPtr pPixmap);

void *exynosUtilPrepareDump(ScrnInfoPtr pScrn, int bo_size, int buf_cnt);
void exynosUtilDoDumpRaws(void *dump, tbm_bo * bo, int *size, int bo_cnt,
                          const char *file);

void exynosUtilDoDumpBmps(void *d, tbm_bo bo, int w, int h, xRectangle *crop,
                          const char *file, const char *dumpType);

void exynosUtilDoDumpPixmaps(void *d, PixmapPtr pPixmap, const char *file,
                             const char *dumpType);

void exynosUtilDoDumpVBuf(void *d, EXYNOSVideoBuf * vbuf, const char *file);
void exynosUtilFlushDump(void *dump);
void exynosUtilFinishDump(void *dump);

int exynosUtilDegreeToRotate(int degree);
int exynosUtilRotateToDegree(int rotate);
int exynosUtilRotateAdd(int rot_a, int rot_b);

void exynosUtilCacheFlush(ScrnInfoPtr scrn);

void *exynosUtilCopyImage(int width, int height,
                          char *s, int s_size_w, int s_size_h,
                          int *s_pitches, int *s_offsets, int *s_lengths,
                          char *d, int d_size_w, int d_size_h,
                          int *d_pitches, int *d_offsets, int *d_lengths,
                          int channel, int h_sampling, int v_sampling);

void exynosUtilRotateArea(int *width, int *height, xRectangle *rect,
                          int degree);
void exynosUtilRotateRect2(int width, int height, xRectangle *rect, int degree,
                           const char *func);
#define exynosUtilRotateRect(w,h,r,d) exynosUtilRotateRect2(w,h,r,d,__FUNCTION__)
void exynosUtilRotateRegion(int width, int height, RegionPtr region,
                            int degree);

void exynosUtilAlignRect(int src_w, int src_h, int dst_w, int dst_h,
                         xRectangle *fit, Bool hw);
void exynosUtilScaleRect(int src_w, int src_h, int dst_w, int dst_h,
                         xRectangle *scale);

const PropertyPtr exynosUtilGetWindowProperty(WindowPtr pWin,
                                              const char *prop_name);

int exynosUtilBoxInBox(BoxPtr base, BoxPtr box);
int exynosUtilBoxArea(BoxPtr pBox);
int exynosUtilBoxIntersect(BoxPtr pDstBox, BoxPtr pBox1, BoxPtr pBox2);
void exynosUtilBoxMove(BoxPtr pBox, int dx, int dy);

Bool exynosUtilRectIntersect(xRectanglePtr pDest, xRectanglePtr pRect1,
                             xRectanglePtr pRect2);

void exynosUtilSaveImage(pixman_image_t * image, char *path);
Bool exynosUtilConvertImage(pixman_op_t op, uchar * srcbuf, uchar * dstbuf,
                            pixman_format_code_t src_format,
                            pixman_format_code_t dst_format, int sw, int sh,
                            xRectangle *sr, int dw, int dh, xRectangle *dr,
                            RegionPtr dst_clip_region, int rotate, int hflip,
                            int vflip);

void exynosUtilConvertBos(ScrnInfoPtr pScrn, int src_id,
                          tbm_bo src_bo, int sw, int sh, xRectangle *sr,
                          int sstride, tbm_bo dst_bo, int dw, int dh,
                          xRectangle *dr, int dstride, Bool composite,
                          int rotate);

void exynosUtilFreeHandle(ScrnInfoPtr scrn, uint32_t handle);

#ifdef LEGACY_INTERFACE
Bool exynosUtilConvertPhyaddress(ScrnInfoPtr scrn, unsigned int phy_addr,
                                 int size, unsigned int *handle);
Bool exynosUtilConvertHandle(ScrnInfoPtr scrn, unsigned int handle,
                             unsigned int *phy_addr, int *size);
#endif

typedef void (*DestroyDataFunc) (void *func_data, uniType key_data);

void *exynosUtilListAdd(void *list, void *key, uniType user_data);
void *exynosUtilListRemove(void *list, void *key);
uniType exynosUtilListGetData(void *list, void *key);
Bool exynosUtilListIsEmpty(void *list);
void exynosUtilListDestroyData(void *list, DestroyDataFunc func,
                               void *func_data);
void exynosUtilListDestroy(void *list);

Bool exynosUtilSetDrmProperty(EXYNOSModePtr pExynosMode, unsigned int obj_id,
                              unsigned int obj_type, const char *prop_name,
                              unsigned int value);

Bool exynosUtilEnsureExternalCrtc(ScrnInfoPtr scrn);

G2dColorMode exynosUtilGetG2dFormat(unsigned int id);
unsigned int exynosUtilGetDrmFormat(unsigned int id);
EXYNOSFormatType exynosUtilGetColorType(unsigned int id);

EXYNOSVideoBuf *exynosUtilCreateVideoBufferByDraw(DrawablePtr pDraw);
EXYNOSVideoBuf *_exynosUtilAllocVideoBuffer(ScrnInfoPtr scrn, int id, int width,
                                            int height, Bool scanout,
                                            Bool reset, Bool exynosure,
                                            const char *func);
EXYNOSVideoBuf *_exynosUtilCreateVideoBuffer(ScrnInfoPtr scrn, int id,
                                             int width, int height,
                                             Bool exynosure, const char *func);
EXYNOSVideoBuf *_exynosUtilVideoBufferRef(EXYNOSVideoBuf * vbuf,
                                          const char *func);
void _exynosUtilVideoBufferUnref(EXYNOSVideoBuf * vbuf, const char *func);
void _exynosUtilFreeVideoBuffer(EXYNOSVideoBuf * vbuf, const char *func);
void exynosUtilClearVideoBuffer(EXYNOSVideoBuf * vbuf);
Bool _exynosUtilIsVbufValid(EXYNOSVideoBuf * vbuf, const char *func);

typedef void (*FreeVideoBufFunc) (EXYNOSVideoBuf * vbuf, void *data);
void exynosUtilAddFreeVideoBufferFunc(EXYNOSVideoBuf * vbuf,
                                      FreeVideoBufFunc func, void *data);
void exynosUtilRemoveFreeVideoBufferFunc(EXYNOSVideoBuf * vbuf,
                                         FreeVideoBufFunc func, void *data);

uniType setunitype32(uint32_t data_u32);

#define exynosUtilAllocVideoBuffer(s,i,w,h,c,r,d)  _exynosUtilAllocVideoBuffer(s,i,w,h,c,r,d,__FUNCTION__)
#define exynosUtilCreateVideoBuffer(s,i,w,h,d)     _exynosUtilCreateVideoBuffer(s,i,w,h,d,__FUNCTION__)
#define exynosUtilVideoBufferUnref(v)  _exynosUtilVideoBufferUnref(v,__FUNCTION__)
#define exynosUtilVideoBufferRef(v)  _exynosUtilVideoBufferRef(v,__FUNCTION__)
#define exynosUtilFreeVideoBuffer(v)   _exynosUtilFreeVideoBuffer(v,__FUNCTION__)
#define exynosUtilIsVbufValid(v)       _exynosUtilIsVbufValid(v,__FUNCTION__)
#define VBUF_IS_VALID(v)            exynosUtilIsVbufValid(v)
#define VSTMAP(v)            ((v)?(v)->stamp:0)
#define VBUF_IS_CONVERTING(v)       (!xorg_list_is_empty (&((v)->convert_info)))

int findActiveConnector(ScrnInfoPtr pScrn);

ScrnInfoPtr exynosUtilDrawToScrn(DrawablePtr pDraw);

/* for debug */
char *exynosUtilDumpVideoBuffer(char *reply, int *len);

#define list_rev_for_each_entry_safe(pos, tmp, head, member) \
    for (pos = __container_of((head)->prev, pos, member), tmp = __container_of(pos->member.prev, pos, member);\
         &pos->member != (head);\
         pos = tmp, tmp = __container_of(pos->member.prev, tmp, member))

#endif                          /* __SEC_UTIL_H__ */
