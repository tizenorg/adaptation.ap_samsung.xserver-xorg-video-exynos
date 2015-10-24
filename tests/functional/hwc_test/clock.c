/**************************************************************************

 watch

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

 Contact: Roman Marchenko <r.marchenko@samsung.com>
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

// this file implements analog watch for using in semiautomatic tests for
// keep safe ddx driver devoloping

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <math.h>

#include "aux_apps.h"
#include "dri2_dri3.h"


#define DEGREE_PER_SECOND (6)


typedef struct _point
{
    int x, y;
    int color;
} PointRec, *PointPtr;

typedef struct _circle
{
    PointRec center;
    int R;
} CircleRec, *CirclePtr;

typedef struct _warch
{
    CircleRec crcl;
    int time;
} WatchRec, *WatchPtr;


xcb_connection_t* dpy = NULL;
xcb_screen_t* screen = NULL;
xcb_rectangle_t wnd_pos = {100, 100, 200, 200};

// work behavior flags: (see aux_apps.c for details)

// mode of work (dri2/dri3+present)
int mode = DRI3_PRESENT_MODE;   // by default dri3 + present will be used
int stop;
WatchRec watch;
uint32_t present_bufs = 3;
uint32_t swap_interval = 1;
uint32_t root_parent = 1;

// functions set for render watch:

inline void
set_point(PointPtr p, uint32_t *map, uint32_t pitch, uint32_t size)
{
    int off = (pitch * p->y + p->x);
    if (off < size && off >= 0)
        map[off] = p->color;
}

inline void
draw_point(PointPtr p, uint32_t *map, uint32_t pitch, uint32_t size)
{
    set_point(p, map, pitch, size);

    p->x += 1;
    set_point(p, map, pitch, size);
    p->x -= 1;

    p->y += 1;
    set_point(p, map, pitch, size);
    p->y -= 1;

    p->x -= 1;
    set_point(p, map, pitch, size);
    p->x += 1;

    p->y -= 1;
    set_point(p, map, pitch, size);
    p->y += 1;
}

void
draw_circle(CirclePtr c, uint32_t *map, uint32_t pitch, uint32_t size)
{
    double x0 = c->center.x;
    double y0 = c->center.y;
    double R = c->R;

    PointRec p = { 0, 0, c->center.color };

    double fi;
    int a;
    for (a = 0; a < 360; a += DEGREE_PER_SECOND)
    {
        fi = M_PI / 180 * a;
        p.x = (int) (x0 + R * cos(fi));
        p.y = (int) (y0 + R * sin(fi));
        draw_point(&p, map, pitch, size);
    }
}

void
draw_watch(WatchPtr w, uint32_t *map, uint32_t pitch, uint32_t size)
{
    draw_circle(&w->crcl, map, pitch, size);

    //drawing hand of the clock, use the change of radius
    PointRec p = { 0, 0, w->crcl.center.color };

    double fi = M_PI / 180 * (w->time * DEGREE_PER_SECOND);

    int R;
    for (R = 0; R < (w->crcl.R - 3); R += 10)
    {
        p.x = (int) (w->crcl.center.x + R * cos(fi));
        p.y = (int) (w->crcl.center.y + R * sin(fi));
        draw_point(&p, map, pitch, size);
    }
}

/*
//
//===================================================================
void
rotate (void)
{
    #define REFRESHE_TIME (16.0) // in ms

    struct timespec current;
    static struct timespec prev;
    double time_diff;

    clock_gettime (CLOCK_REALTIME, &current);

    // get difference in nanoseconds
    time_diff = (double)( ( current.tv_sec - prev.tv_sec ) * 1000000000 +
                          ( current.tv_nsec - prev.tv_nsec ) );

    if (time_diff / 1000000.0 > REFRESHE_TIME)
    {
        prev = current;

        watch.time++;
    }

    #undef REFRESHE_TIME
}
*/

// direct accessing memory clear callback
// bo_e - contains all information to do direct render in memory
//===================================================================
void
raw_clear (const bo_t* bo_e)
{
    xcb_rectangle_t rect;

    if (!bo_e)
    {
        printf ("raw_clear: invalid parameters.\n");
        return;
    }

    rect.x = 0;
    rect.y = 0;
    rect.width = bo_e->width;
    rect.height = bo_e->height;

    // simple draw black rectangle with size as window has
    raw_fill_rect (bo_e, 1, &rect, screen->black_pixel);
}

// direct accessing memory draw callback (watch drawing)
// bo_e - contains all information to do direct render in memory
//===================================================================
void
raw_draw (const bo_t* bo_e)
{
    tbm_bo_handle hndl;
    int* temp_map;
    int i;

    if (!bo_e)
    {
        printf ("raw_draw: invalid parameters.\n");
        return;
    }

    watch.crcl.R = (bo_e->width > bo_e->height) ? bo_e->height/2 : bo_e->width/2;
    watch.crcl.center.x = bo_e->width / 2;
    watch.crcl.center.y = bo_e->height / 2;

    hndl = tbm_bo_map (bo_e->bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    if (!hndl.ptr)
    {
        printf ("raw_draw_line: Error while tbm_bo_map.\n");
        exit (1);
    }

    // clear buffer
    // we set all transparency bits to make buffer content fully
    // un-transparency, actually to see this content over other buffers
    temp_map = (int*)hndl.ptr;
    for (i = 0; i < (bo_e->stride * bo_e->height)/sizeof(int); i++)
        *temp_map++ = 0xff000000;

    draw_watch (&watch, (uint32_t*)hndl.ptr, bo_e->stride/4,
                tbm_bo_size (bo_e->bo)/4);

    if (!stop)
        watch.time++;//rotate ();

    tbm_bo_unmap (bo_e->bo);
}

//
//===================================================================
int
main (int argc, char** argv)
{
    xcb_generic_event_t* event = NULL;
    xcb_window_t main_wnd;
    draw_funcs_t draw_func;
    dri2_dri3_params_t params;
    int res;

    cmd_parse (argc, argv);

    dpy = xcb_connect (NULL, NULL);

    // get first screen of display
    screen = xcb_setup_roots_iterator (xcb_get_setup (dpy)).data;

    main_wnd = create_window (dpy, 0xff, root_parent, "clock");

    watch.crcl.R = (wnd_pos.width > wnd_pos.height) ? wnd_pos.height/2 : wnd_pos.width/2;
    watch.crcl.center.x = wnd_pos.width / 2;
    watch.crcl.center.y = wnd_pos.height / 2;
    watch.crcl.center.color = 0xff00ff00;   // set all transparency bits

    draw_func.raw_clear = NULL;
    draw_func.raw_draw = raw_draw;
    draw_func.xcb_clear = NULL;
    draw_func.xcb_draw = NULL;

    params.num_of_bufs = present_bufs;
    params.swap_interval = swap_interval;

    // inside init_dri2_dri3 new thread, responsible for dri2/dri3/present events
    // handling, will be created
    res = init_dri2_dri3 (dpy, &draw_func, mode, main_wnd, &params);
    if (res)
    {
        printf ("init_dri2_dri3: %s.\n", get_last_error());
        exit (1);
    }

    printf ("before xcb loop.\n");

    while (1)
    {
        xcb_flush (dpy);
        event = xcb_wait_for_event (dpy);
        if (!event) break;

        switch (event->response_type)
        {
            case XCB_EXPOSE:
                DEBUG_OUT ("XCB_MAP_WINDOW\n");
            break;

            // if we are notified by XCB_BUTTON_PRESS event, we start/stop
            // to change our frames, simple draw the same thing
            case XCB_BUTTON_PRESS:
                stop ^= 1;
            break;

            default:
            break;
        }
    }

    return 0;
}
