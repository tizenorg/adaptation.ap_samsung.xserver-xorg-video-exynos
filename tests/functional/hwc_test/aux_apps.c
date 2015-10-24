/**************************************************************************

 auxilary module's source

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

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <xcb/composite.h>

#include "aux_apps.h"
#include "dri2_dri3.h"

extern xcb_rectangle_t wnd_pos;
extern int mode;
extern uint32_t present_bufs;
extern uint32_t swap_interval;
extern uint32_t root_parent;

// get name of app, which uses this library
//========================================================================
char*
app_name (void)
{
    FILE* f;
    char* slash;
    static int 	initialized;
    static char app_name[128];

    if (initialized)
        return app_name;

    // get the application name
    f = fopen ("/proc/self/cmdline", "r");

    if (!f)
        return NULL;

    if (fgets (app_name, sizeof(app_name), f) == NULL)
    {
        fclose (f);
        return NULL;
    }

    fclose (f);

    if ((slash = strrchr (app_name, '/')) != NULL)
    {
        memmove (app_name, slash + 1, strlen(slash));
    }

    initialized = 1;

    return app_name;
}

//
//========================================================================
void
print_help (void)
{
    printf ("%s - test application.\n"
            "This app can be used to test dri2, dri3, present and hwc extensions.\n"
            "You can choose work mode among dri2, present or dri3 + present modes.\n"
            "You can use this app wiht hwc-sample to test hwc extension (with dri2, present or dri3 + present).\n"
            "You can set amount of buffers for present work mode, by default app uses triple buffering in present\n"
            "mode, amount of buffers in dri2 mode depends on ddx driver implementation.\n"
            "You can use this app with square-bubbles to test properly window's size & position changes.\n"
            "To test flip mode, you must launch app in full-screen mode.\n"
            "By default frame rate is limited to (vblank)vertical blank (60 Hz), so you cann't see fps more then 60,\n"
            "you can disable binding to vblank - set swap interval to 0.\n\n"
            "available options:\n"
            "\t -geo 'width'x'height'          -- set window's size,\n"
            "\t -geo 'width'x'height' 'x' 'y'  -- set window's geometry,\n"
            "\t -dri3                          -- dri3 + present Exts. mode (default mode),\n"
            "\t -dri2                          -- dri2 Exts. mode,\n"
            "\t -present                       -- present Exts. mode,\n"
            "\t -present_bufs                  -- amount of buffers used in no-dri2 mode (default value: 3),\n"
            "\t -s_i                           -- buffers swap interval (both dri2 and present) (default value: 1),\n"
            "\t -root_parent                   -- is parent a root window (default value: 1).\n", app_name());
}

// parse command line
// if you want to do some specific for your app - implement yourself's cmd parsing
//========================================================================
void
cmd_parse (int argc, char* const argv[])
{
    int i;

    if (argc < 2) return;

    // if we want to set window's size and position
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-geo"))
        {
            if ((i + 1 < argc) && (i + 2 >= argc))
                sscanf (argv[i + 1], "%hux%hu", &wnd_pos.width, &wnd_pos.height);
            else if (i + 3 < argc)
            {
                sscanf (argv[i + 1], "%hux%hu", &wnd_pos.width, &wnd_pos.height);
                sscanf (argv[i + 2], "%hd", &wnd_pos.x);
                sscanf (argv[i + 3], "%hd", &wnd_pos.y);
            }
            break;
        }
    }

    // if we want to use dri3 + present Exts.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-dri3"))
        {
            mode = DRI3_PRESENT_MODE;
            break;
        }
    }

    // if we want to use dri2 Ext.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-dri2"))
        {
            mode = DRI2_MODE;
            break;
        }
    }

    // if we want to use present Exts.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-present"))
        {
            mode = PRESENT_MODE;
            break;
        }
    }

    // if we want to set amount of buffers for present usage.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-present_bufs") && (argc - i > 1))
        {
            sscanf (argv[i + 1], "%u", &present_bufs);
            break;
        }
    }

    // if we want to set buffer swap interval.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-s_i") && (argc - i > 1))
        {
            sscanf (argv[i + 1], "%u", &swap_interval);
            break;
        }
    }

    // if we want to insert window between root and window to render.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-root_parent") && (argc - i > 1))
        {
            sscanf (argv[i + 1], "%u", &root_parent);
            break;
        }
    }

    // if we want to print help
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "--help") || !strcmp (argv[i], "-help") || !strcmp (argv[i], "-h"))
        {
            print_help ();
            exit (0);
        }
    }
}

// check is composite extension existed
//========================================================================
int
check_composite_ext (xcb_connection_t* dpy)
{
    if (!dpy) return 0;

    xcb_composite_query_version_cookie_t q_c;
    xcb_composite_query_version_reply_t* q_r;
    xcb_generic_error_t* xcb_errors = NULL;

    q_c = xcb_composite_query_version (dpy, XCB_COMPOSITE_MAJOR_VERSION, XCB_COMPOSITE_MINOR_VERSION);
    q_r = xcb_composite_query_version_reply (dpy, q_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while composite_query_version call.\n");
        return 0;
    }
    else
        printf ("composite extension: %d.%d.\n", q_r->major_version, q_r->minor_version);

    free (q_r);

    return 1;
}

// create window with 'background' background
// root_parent - if cleared, then two windows will be created (parent and child),
// and id of child window will be returned
//===================================================================
xcb_window_t
create_window (xcb_connection_t* dpy, uint32_t background, uint32_t root_parent, char* name)
{
    uint32_t window_mask;
    uint32_t window_values[3];
    xcb_window_t wnd, wnd_child;
    xcb_screen_t* screen;
    uint16_t border_width;
    uint32_t gravity;
    int is_composite_ext = 0;

    if (!dpy || !name)
        return 0;

    if(root_parent)
        is_composite_ext = 0;//check_composite_ext (dpy);

    // copy xeyes behavior... :-)
    border_width = root_parent ? 0 : 1;
    gravity = root_parent ? XCB_GRAVITY_BIT_FORGET : XCB_GRAVITY_NORTH_WEST;

    // get first screen of display
    screen = xcb_setup_roots_iterator (xcb_get_setup (dpy)).data;

    window_mask = XCB_CW_BACK_PIXEL | XCB_CW_BIT_GRAVITY | XCB_CW_EVENT_MASK;
    window_values[0] = 0x00;//background;
    window_values[1] = gravity;
    window_values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS;

    wnd = xcb_generate_id (dpy);
    xcb_create_window (dpy, DEPTH, wnd, screen->root, wnd_pos.x, wnd_pos.y,
                       wnd_pos.width, wnd_pos.height, border_width, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                       screen->root_visual, window_mask, window_values);

    // hwc-sample and square_bubbles manipulate only named windows
    xcb_change_property (dpy, XCB_PROP_MODE_REPLACE, wnd, XCB_ATOM_WM_NAME,
                         XCB_ATOM_STRING, 8, strlen (name), name);

    // see XSizeHints struct definintion in X11/Xutil.h
    // The x, y, width, and height members are now obsolete, but wihtout them
    // WM doesn't listen our request about keep window's size.
    uint32_t x_size_hints[] =
    {
        3, // user specified x, y & user specified width, height
        (uint32_t)wnd_pos.x, (uint32_t)wnd_pos.y,
        (uint32_t)wnd_pos.width, (uint32_t)wnd_pos.height,
        0, 0,       // int min_width, min_height;
        0, 0,       // int max_width, max_height;
        0, 0,       // int width_inc, height_inc;
        0,0, 0,0,   // struct { int x; int y; } min_aspect, max_aspect;
        0, 0,       // int base_width, base_height;
        0           // int win_gravity;
    };

    // set WM_NORMAL_HINTS property
    xcb_change_property (dpy, XCB_PROP_MODE_REPLACE, wnd, XCB_ATOM_WM_NORMAL_HINTS,
                         XCB_ATOM_WM_SIZE_HINTS, 32, sizeof(x_size_hints)/sizeof(uint32_t),
                         x_size_hints);

    if(root_parent && is_composite_ext)
    {
        printf ("wnd: 0x%x will be redirected.\n", wnd);
        xcb_composite_redirect_window (dpy, wnd, XCB_COMPOSITE_REDIRECT_MANUAL);
    }

    // Make window visible
    xcb_map_window (dpy, wnd);

    // copy xeyes behavior... :-)
    if(!root_parent)
    {
        window_values[1] = XCB_GRAVITY_BIT_FORGET;

        wnd_child = xcb_generate_id (dpy);
        xcb_create_window (dpy, DEPTH, wnd_child, wnd, 0, 0,
                           wnd_pos.width, wnd_pos.height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                           screen->root_visual, window_mask, window_values);

        // Make window visible
        xcb_map_window (dpy, wnd_child);

        return wnd_child;
    }

    return wnd;
}

