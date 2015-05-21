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

#include "sec_video_types.h"

typedef struct _SECWb SECWb;

typedef enum
{
    WB_NOTI_INIT,
    WB_NOTI_START,
    WB_NOTI_IPP_EVENT,
    WB_NOTI_IPP_EVENT_DONE,
    WB_NOTI_PAUSE,
    WB_NOTI_STOP,
    WB_NOTI_CLOSED,
} SECWbNotify;

typedef void (*WbNotifyFunc) (SECWb *wb, SECWbNotify noti, void *noti_data, void *user_data);

/* Don't close the wb from secWbGet */
SECWb* secWbGet       (void);

/* If width, height is 0, they will be main display size. */
SECWb* _secWbOpen     (ScrnInfoPtr pScrn,
                      unsigned int id, int width, int height,
                      Bool scanout, int hz, Bool need_rotate_hook,
                      const char *func);
void   _secWbClose    (SECWb *wb, const char *func);
Bool   _secWbStart    (SECWb *wb, const char *func);
void   _secWbStop     (SECWb *wb, Bool close_buf, const char *func);

#define secWbOpen(s,i,w,h,c,z,n)    _secWbOpen(s,i,w,h,c,z,n,__FUNCTION__)
#define secWbClose(w)               _secWbClose(w,__FUNCTION__)
#define secWbStart(w)               _secWbStart(w,__FUNCTION__)
#define secWbStop(w,c)              _secWbStop(w,c,__FUNCTION__)

Bool   secWbSetBuffer (SECWb *wb, SECVideoBuf **vbufs, int bufnum);

Bool   secWbSetRotate (SECWb *wb, int rotate);
int    secWbGetRotate (SECWb *wb);

void   secWbSetTvout (SECWb *wb, Bool enable);
Bool   secWbGetTvout (SECWb *wb);

void   secWbSetSecure (SECWb *wb, Bool secure);
Bool   secWbGetSecure (SECWb *wb);

void   secWbGetSize  (SECWb *wb, int *width, int *height);

Bool   secWbCanDequeueBuffer (SECWb *wb);
void   secWbQueueBuffer (SECWb *wb, SECVideoBuf *vbuf);

void   secWbAddNotifyFunc (SECWb *wb, SECWbNotify noti, WbNotifyFunc func, void *user_data);
void   secWbRemoveNotifyFunc (SECWb *wb, WbNotifyFunc func);

Bool   secWbIsOpened  (void);
Bool   secWbIsRunning (void);
void   secWbDestroy   (void);

void   secWbPause (SECWb *wb);
void   secWbResume (SECWb *wb);

unsigned int secWbGetPropID (void);
void   secWbHandleIppEvent (int fd, unsigned int *buf_idx, void *data);

#endif // __SEC_WB_H__
