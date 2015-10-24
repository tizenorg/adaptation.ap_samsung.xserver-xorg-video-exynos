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
#ifndef __SEC_WB_H__
#define __SEC_WB_H__

#include <sys/types.h>
#include <xf86str.h>

#include "exynos_video_types.h"

typedef struct _EXYNOSWb EXYNOSWb;

typedef enum {
    WB_NOTI_INIT,
    WB_NOTI_START,
    WB_NOTI_IPP_EVENT,
    WB_NOTI_IPP_EVENT_DONE,
    WB_NOTI_PAUSE,
    WB_NOTI_STOP,
    WB_NOTI_CLOSED,
} EXYNOSWbNotify;

typedef void (*WbNotifyFunc) (EXYNOSWb * wb, EXYNOSWbNotify noti,
                              void *noti_data, void *user_data);

/* Don't close the wb from exynosWbGet */
EXYNOSWb *exynosWbGet(void);

/* If width, height is 0, they will be main display size. */
EXYNOSWb *_exynosWbOpen(ScrnInfoPtr pScrn,
                        unsigned int id, int width, int height,
                        Bool scanout, int hz, Bool need_rotate_hook,
                        const char *func);
void _exynosWbClose(EXYNOSWb * wb, const char *func);
Bool _exynosWbStart(EXYNOSWb * wb, const char *func);
void _exynosWbStop(EXYNOSWb * wb, Bool close_buf, const char *func);

#define exynosWbOpen(s,i,w,h,c,z,n)    _exynosWbOpen(s,i,w,h,c,z,n,__FUNCTION__)
#define exynosWbClose(w)               _exynosWbClose(w,__FUNCTION__)
#define exynosWbStart(w)               _exynosWbStart(w,__FUNCTION__)
#define exynosWbStop(w,c)              _exynosWbStop(w,c,__FUNCTION__)

Bool exynosWbSetBuffer(EXYNOSWb * wb, EXYNOSVideoBuf ** vbufs, int bufnum);

Bool exynosWbSetRotate(EXYNOSWb * wb, int rotate);
int exynosWbGetRotate(EXYNOSWb * wb);

void exynosWbSetTvout(EXYNOSWb * wb, Bool enable);
Bool exynosWbGetTvout(EXYNOSWb * wb);

void exynosWbSetSecure(EXYNOSWb * wb, Bool exynosure);
Bool exynosWbGetSecure(EXYNOSWb * wb);

void exynosWbGetSize(EXYNOSWb * wb, int *width, int *height);

Bool exynosWbCanDequeueBuffer(EXYNOSWb * wb);
void exynosWbQueueBuffer(EXYNOSWb * wb, EXYNOSVideoBuf * vbuf);

void exynosWbAddNotifyFunc(EXYNOSWb * wb, EXYNOSWbNotify noti,
                           WbNotifyFunc func, void *user_data);
void exynosWbRemoveNotifyFunc(EXYNOSWb * wb, WbNotifyFunc func);

Bool exynosWbIsOpened(void);
Bool exynosWbIsRunning(void);
void exynosWbDestroy(void);

void exynosWbPause(EXYNOSWb * wb);
void exynosWbResume(EXYNOSWb * wb);

unsigned int exynosWbGetPropID(void);
void exynosWbHandleIppEvent(int fd, unsigned int *buf_idx, void *data);

#endif                          // __SEC_WB_H__
