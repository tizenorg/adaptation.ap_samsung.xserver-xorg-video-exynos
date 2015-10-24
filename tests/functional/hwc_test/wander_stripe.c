/**************************************************************************

 wander_strip

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

 Contact: Keith Packard <keithp@keithp.com>
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

// this file implements wander_stripe for using in semiautomatic tests for
// keep safe ddx driver devoloping

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include "aux_apps.h"
#include "dri2_dri3.h"


xcb_connection_t* dpy = NULL;
xcb_screen_t* screen = NULL;
xcb_rectangle_t wnd_pos = {100, 100, 200, 200};
xcb_rectangle_t wander_strip;
xcb_gc_t		gc;

// work behavior flags: (see aux_apps.c for details)

// mode of work (dri2/dri3+present)
int mode = DRI3_PRESENT_MODE;   // by default dri3 + present will be used
int stop;
int x_step = 1;
uint32_t present_bufs = 3;
uint32_t swap_interval = 1;
uint32_t root_parent = 1;


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

    // draw white rectangle with size as window has
    raw_fill_rect (bo_e, 1, &rect, 0xff000000 | screen->white_pixel);
}

// direct accessing memory draw callback (wander stripe drawing)
// bo_e - contains all information to do direct render in memory
//===================================================================
void
raw_draw (const bo_t* bo_e)
{
    static int x;

    if (!bo_e)
    {
        printf ("raw_draw: invalid parameters.\n");
        return;
    }  

    wander_strip.height = bo_e->height;
    wander_strip.width = bo_e->width / 4;

    // move stripe
    if (x_step > 0)
    {
        if (x + wander_strip.width > bo_e->width)
        {
            x_step = -x_step;
            x += x_step;
        }
    }
    else
    {
        if (x < 0)
        {
            x_step = -x_step;
            x += x_step;
        }
    }

    wander_strip.x = x;

    // draw white wrandered rectangle
    raw_fill_rect (bo_e, 1, &wander_strip, 0xff0000ff);

    if (!stop)
        x += x_step;
}

// xcb based draw callback (wander stripe drawing)
// drawable - drawable to render into it
// draw_w, draw_h - size of drawable
//===================================================================
void
xcb_draw (xcb_drawable_t drawable, uint32_t draw_w, uint32_t draw_h)
{
    static int x;
    uint32_t gc_mask;
    uint32_t gc_values;
    xcb_rectangle_t temp_rect;

    // draw white rectangle with size as window has
    gc_mask = XCB_GC_FOREGROUND;
    gc_values = 0xffffffff;

    xcb_change_gc (dpy, gc, gc_mask, &gc_values);
    temp_rect.x = 0;
    temp_rect.y = 0;
    temp_rect.width = draw_w;
    temp_rect.height = draw_h;
    xcb_poly_fill_rectangle(dpy, drawable, gc, 1, &temp_rect);

    // draw black wander strip
    gc_mask = XCB_GC_FOREGROUND;
    gc_values = 0x0;
    xcb_change_gc (dpy, gc, gc_mask, &gc_values);

    wander_strip.height = draw_h;

    // move stripe
    if (x_step > 0)
    {
        if (x + wander_strip.width > draw_w)
        {
            x_step = -x_step;
            x += x_step;
        }
    }
    else
    {
        if (x < 0)
        {
            x_step = -x_step;
            x += x_step;
        }
    }

    wander_strip.x = x;

    // draw white wrandered rectangle
    xcb_poly_fill_rectangle (dpy, drawable, gc, 1, &wander_strip);

    if (!stop)
        x += x_step;

    xcb_flush (dpy);
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

    main_wnd = create_window (dpy, 0xff, root_parent, "wander stripe");

    gc = xcb_generate_id (dpy);
    xcb_create_gc (dpy, gc, main_wnd, 0, NULL);

    draw_func.raw_clear = raw_clear;
    draw_func.raw_draw = raw_draw;
    draw_func.xcb_draw = xcb_draw;
    draw_func.xcb_clear = NULL;

    // init for drawing
    wander_strip.y = 0;
    wander_strip.width = wnd_pos.width / 4;
    wander_strip.height = wnd_pos.height;

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
