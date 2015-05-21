#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include "dri2_dri3.h"


// if you want to do some specific for your app - implement yourself's cmd parsing
extern void cmd_parse (int argc, char* const argv[]);


xcb_connection_t* dpy = NULL;
xcb_screen_t* screen = NULL;
xcb_rectangle_t wnd_pos = {100, 100, 200, 200};
xcb_rectangle_t wander_strip;
xcb_gc_t		gc;

// mode of work (dri2/dri3+present)
int mode = DRI3_PRESENT_MODE;   // by default dri3 + present will be used
int stop;
int x_step = 1;
uint32_t present_bufs = 3;


//
//===================================================================
xcb_window_t
create_window (uint32_t background)
{
    uint32_t window_mask;
    uint32_t window_values[2];
    xcb_window_t wnd;

    window_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
    window_values[0] = background;
    window_values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS;

    wnd = xcb_generate_id (dpy);
    xcb_create_window (dpy, DEPTH, wnd, screen->root, wnd_pos.x, wnd_pos.y,
                       wnd_pos.width, wnd_pos.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                       screen->root_visual, window_mask, window_values);

    // hwc-sample and square_bubbles manipulate only named windows
    xcb_change_property (dpy, XCB_PROP_MODE_REPLACE, wnd, XCB_ATOM_WM_NAME,
                         XCB_ATOM_STRING, 8, strlen ("rotated snowflake"), "rotated snowflake");

    // Make window visible
    xcb_map_window (dpy, wnd);

    return wnd;
}

//
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
    rect.width = wnd_pos.width;
    rect.height = wnd_pos.height;

    // draw white rectangle with size as window has
    raw_fill_rect (bo_e, 1, &rect, screen->white_pixel);
}

//
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
    raw_fill_rect (bo_e, 1, &wander_strip, screen->black_pixel);

    if (!stop)
        x += x_step;
}

//
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
    temp_rect.width = wnd_pos.width;
    temp_rect.height = wnd_pos.height;
    xcb_poly_fill_rectangle(dpy, drawable, gc, 1, &temp_rect);

    // draw black wander strip
    gc_mask = XCB_GC_FOREGROUND;
    gc_values = 0x0;
    xcb_change_gc (dpy, gc, gc_mask, &gc_values);

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
    int res;

    cmd_parse (argc, argv);

    dpy = xcb_connect (NULL, NULL);

    // get first screen of display
    screen = xcb_setup_roots_iterator (xcb_get_setup (dpy)).data;

    main_wnd = create_window (0xff);

    gc = xcb_generate_id (dpy);
    xcb_create_gc (dpy, gc, main_wnd, 0, NULL);

    draw_func.raw_clear = raw_clear;
    draw_func.raw_draw = raw_draw;
    draw_func.xcb_draw = xcb_draw;
    draw_func.xcb_clear = NULL;

    // init for drawing
    wander_strip.y = 0;
    wander_strip.width = 60;
    wander_strip.height = wnd_pos.height;

    res = init_dri2_dri3 (dpy, &draw_func, mode, main_wnd, present_bufs);
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
                printf ("XCB_MAP_WINDOW\n");
            break;

            case XCB_BUTTON_PRESS:
                stop ^= 1;
            break;

            default:
            break;
        }
    }

    return 0;
}
