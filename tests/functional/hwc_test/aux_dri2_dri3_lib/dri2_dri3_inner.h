/**************************************************************************

 dri2_dri3 library's inner header

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

 Contact: Sergey Sizonov <s.sizonov@samsung.com>

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

#ifndef _DRI2_DRI3_INNER_H
#define _DRI2_DRI3_INNER_H

#include <xf86drm.h>

#include "dri2_dri3.h"
#include "fps.h"

#ifdef _DEBUG_
    #define TIME_START()\
        {\
            struct timespec start;\
            clock_gettime (CLOCK_REALTIME, &start);
    #define TIME_END(str) \
            DEBUG_OUT ("%s: %f.\n", str, get_time_diff (&start) / 1000000.0f);\
        }
#else
    #define TIME_START() {;}
    #define TIME_END(...) {;}
#endif

typedef struct display_t
{
    xcb_connection_t* dpy;
    xcb_connection_t* client_dpy;
    xcb_screen_t* screen;
    xcb_window_t wnd;
} display_t;

typedef struct graphics_ctx_t
{
    int mode;
    draw_funcs_t draw_funcs;
    dri2_dri3_params_t params;
} graphics_ctx;

extern display_t display;
extern graphics_ctx gr_ctx;
extern pthread_t thread_id;

extern xcb_rectangle_t wnd_pos;

void get_wnd_geometry (xcb_window_t win_id, xcb_rectangle_t* geometry);
void set_error (const char* str);

int prepare_dri2_ext (void);
void dri2_loop (void);

int prepare_dri3_present_ext (void);
void dri3_loop (void);

#endif
