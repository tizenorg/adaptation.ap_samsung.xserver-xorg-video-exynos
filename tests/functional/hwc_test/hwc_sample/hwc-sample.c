/**************************************************************************

 hwc_sample

 Copyright 2010 - 2015 Samsung Electronics co., Ltd. All Rights Reserved.

 Contact: SooChan Lim <sc1.lim@samsung.com>
 Contact: Olexandr Rozov <o.rozov@samsung.com>
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

/* this file contains implementation of hwc "composition manager".
   this app uses hwc and composite extensions to create image of screen
   from window's contents by using drm planes.*/

#include "hwc-sample.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h> 
#include <pthread.h>
#include <sys/types.h>
#include <X11/keysym.h>
#include <X11/extensions/Xcomposite.h>

// control window's names
#define CHANGE_SIZE_WND         "Change size"
#define MOVE_WND                "Move"
#define CHANGE_STACK_ORDER_WND  "Change stack order"

#define _DEBUG 1
#define KEYCODE XK_Down

int wd1 = 100, ht1 = 100;
int dispsize_width = 720, dispsize_height = 1280;

struct app_info ai;

Display *g_dpy = NULL;
int depth;
int screen;

int hwc_op_code, hwc_event, hwc_error;
Window root, change_size, move_btn, change_stack_order;
int len1, width1, font_height;
char str1[25];

XColor red, brown, blue;
/* used for allocation of the given color    */
/* map entries.                              */

static ThreadData thread_data;

// information about all external windows (with names)
Window* root_childrens;
unsigned int nchildrens;

int find_all_window_ids (void);

static void
create_hwcthread (ThreadData *data)
{
    pthread_t thread;

    if (pthread_create (&thread, NULL, thread1_hwcsample, data) < 0)
    {
        printf ("creating thread error\n");
        exit (-1);
    }

    pthread_detach (thread);
}

static void
fill (Drawable draw, int sx, int sy, int width, int height)
{
    //XFontStruct *fontinfo;
    GC gr_context;
    XGCValues gr_values;

    int scr = XDefaultScreen (g_dpy);

    gr_values.function = GXcopy;
    gr_values.plane_mask = AllPlanes;
    gr_values.foreground = BlackPixel(g_dpy, scr);
    gr_values.background = WhitePixel(g_dpy, scr);
    gr_context = XCreateGC (g_dpy, draw, 0, &gr_values);
//    srand (time(NULL));
    XSetForeground (g_dpy, gr_context, rand () % 0xFFFF); //0xFF000000);
    XSetBackground (g_dpy, gr_context, 0x00FFFFFF); //0xFF000000);
    XFlush (g_dpy);

//    fontinfo = XLoadQueryFont(g_dpy, "6x10");
//    XSetFont(g_dpy, gr_context, fontinfo->fid);
//    XDrawString(g_dpy, pixmap, gr_context, 50, 50, "Hello", 5);
//    XFlush(g_dpy);

    XFillRectangle (g_dpy, draw, gr_context, sx, sy, width, height);

    //
    if (ai.hwc_set == 2)
    {
        Pixmap temp_px = XCreatePixmap (g_dpy, draw, width, height, depth);
        XDrawLine (g_dpy, temp_px, gr_context, 0, 0, width, height);
        XCopyArea (g_dpy, temp_px, draw, gr_context, 0, 0, width, height, 0, 0);
        XFreePixmap (g_dpy, temp_px);
    }
    else
        XDrawLine (g_dpy, draw, gr_context, sx, sy, sx + width, sy + height);

    XFlush (g_dpy);
}

static void
make_pixmaps (int num)
{
    XRectangle tmp_d = {150, 150, 256, 256};
    XRectangle tmp_s = {0, 0, 256, 256};

    ai.count = num;

    if (ai.use_root)
    {
        ai.src[0].x = 0;
        ai.src[0].y = 0;
        ai.src[0].width = dispsize_width;
        ai.src[0].height = dispsize_height;
        ai.dst[0].x = 0;
        ai.dst[0].y = 0;
        ai.dst[0].width = dispsize_width;
        ai.dst[0].height = dispsize_height;
        ai.draws[0] = XDefaultRootWindow (g_dpy);
        ai.count++;
    }

    int i;
    for (i = ai.use_root; i < ai.count; i++)
    {
        ai.draws[i] = XCreatePixmap (g_dpy, DefaultRootWindow(g_dpy), tmp_s.width, tmp_s.height,
                                     DefaultDepth(g_dpy, DefaultScreen (g_dpy)));
        XFlush (g_dpy);
        fill (ai.draws[i], tmp_s.x, tmp_s.y, tmp_s.width, tmp_s.height);
        ai.dst[i] = tmp_d;
        ai.src[i] = tmp_s;
        //shift dst
        tmp_d.x += 50;
        tmp_d.y += 50;
        printf ("pixmap%d x=%d,y=%d width=%d height=%d \n", i, tmp_s.x, tmp_s.y, tmp_s.width, tmp_s.height);

    }
    printf ("make pixmaps done \n");
}

static void
make_windows (int num)
{
    XRectangle tmp_d = {150, 150, 256, 256};
    XRectangle tmp_s = {0, 0, 256, 256};
    XSetWindowAttributes attributes;

    ai.count = num;
    int i;
    if (ai.use_root)
    {
        ai.src[0].x = 0;
        ai.src[0].y = 0;
        ai.src[0].width = dispsize_width;
        ai.src[0].height = dispsize_height;
        ai.dst[0].x = 0;
        ai.dst[0].y = 0;
        ai.dst[0].width = dispsize_width;
        ai.dst[0].height = dispsize_height;
        ai.draws[0] = XDefaultRootWindow (g_dpy);
        ai.count++;
    }

    for (i = ai.use_root; i < ai.count; i++)
    {
        attributes.background_pixel = rand () % 0x00ffffff; //XWhitePixel (g_dpy, XDefaultScreen (g_dpy));
        attributes.border_pixel = XBlackPixel (g_dpy, XDefaultScreen (g_dpy));
        attributes.override_redirect = 0;
        ai.draws[i] = XCreateWindow (g_dpy, XDefaultRootWindow (g_dpy), tmp_s.x, tmp_s.y, tmp_s.width, tmp_s.height, 0,
                                     DefaultDepth(g_dpy, DefaultScreen(g_dpy)), InputOutput,
                                     DefaultVisual(g_dpy, DefaultScreen(g_dpy)),
                                     CWBackPixel /*| CWBorderPixel*/| CWOverrideRedirect,
                                     &attributes);
        XFlush (g_dpy);
        XSelectInput (g_dpy, ai.draws[i],
        StructureNotifyMask | ExposureMask | ButtonMotionMask | PointerMotionMask | ButtonPressMask);

        fill (ai.draws[i], tmp_s.x, tmp_s.y, tmp_s.width, tmp_s.height);
        ai.dst[i] = tmp_d;
        ai.src[i] = tmp_s;
        //shift dst
        tmp_d.x += 50;
        tmp_d.y += 50;

        printf ("window%d x=%d,y=%d width=%d height=%d \n", i, tmp_s.x, tmp_s.y, tmp_s.width, tmp_s.height);
        XFlush (g_dpy);
    }
    printf ("make windows done \n");
}

static void
hwc_loop ()
{
#if 0
    HWCSelectInput(dpy, win, HWCAllEvents);

    XEvent e;
    int stat;

    while(1)
    {
        XNextEvent(dpy, &e);
        switch(e.type)
        {
            case MapNotify:
            print_time("MapNotify");
            break;
            case GenericEvent:
            {
                XGenericEvent *ge = (XGenericEvent*)&e;
                printf("[HWC] EVENT(extension:%d, evtype:%d)\n", ge->extension, ge->evtype);
                if(ge->extension == hwc_op_code)
                {
                    switch(ge->evtype)
                    {
                        case HWCConfigureNotify:
                        if(XGetEventData(dpy, &(e.xcookie)))
                        {
                            HWCConfigureNotifyCookie* data = (HWCConfigureNotifyCookie*)e.xcookie.data;
                            printf("[HWC] EVENT_HWCConfigureNotify(cookie:%d, evtype:%d, maxLayer:%d)\n", e.xcookie.cookie, data->evtype, data->maxLayer);
                            XFreeEventData(dpy, &(e.xcookie));
                        }
                        break;
                    }
                }
            }
            break;
        }

    }
#endif
}

void
hwc_set (void)
{
    //printf ("%s,%d\n", __func__, __LINE__);
#ifdef _DEBUG
    int i;

    for (i = 0; i < ai.count; i++)
        printf ("HWCSetDrawable drawable[%d] = 0x%06lx srcrest={%4d,%4d,%4d,%4d} dstrect={%4d,%4d,%4d,%4d} \n", i,
                ai.draws[i], ai.src[i].x, ai.src[i].y, ai.src[i].width, ai.src[i].height, ai.dst[i].x, ai.dst[i].y,
                ai.dst[i].width, ai.dst[i].height);
#endif
    HWCCompositeMethod method[5] = {HWC_COMPOSITE_METHOD_DEFAULT, };

    HWCSetDrawables (g_dpy, 0, root, ai.draws, ai.src, ai.dst, method, ai.count);

    XFlush (g_dpy);
}

void
hwc_movew (void)
{
    printf ("%s,%d\n", __func__, __LINE__);

    printf ("HWCMoveWindow window = 0x%06lx srcrest={%d,%d,%d,%d},\t dstrect={%d,%d,%d,%d} \n",
            ai.draws[ai.wnd_to_change], ai.src[ai.wnd_to_change].x, ai.src[ai.wnd_to_change].y,
            ai.src[ai.wnd_to_change].width, ai.src[ai.wnd_to_change].height, ai.dst[ai.wnd_to_change].x,
            ai.dst[ai.wnd_to_change].y, ai.dst[ai.wnd_to_change].width, ai.dst[ai.wnd_to_change].height);
    XMoveWindow (g_dpy, ai.draws[ai.wnd_to_change], ai.dst[ai.wnd_to_change].x, ai.dst[ai.wnd_to_change].y);

    XFlush (g_dpy);
}

void
hwc_resizew (void)
{
    printf ("%s,%d\n", __func__, __LINE__);

    printf ("HWCResizeMoveWindow window = 0x%06lx srcrest={%d,%d,%d,%d},\t dstrect={%d,%d,%d,%d} \n",
            ai.draws[ai.wnd_to_change], ai.src[ai.wnd_to_change].x, ai.src[ai.wnd_to_change].y,
            ai.src[ai.wnd_to_change].width, ai.src[ai.wnd_to_change].height, ai.dst[ai.wnd_to_change].x,
            ai.dst[ai.wnd_to_change].y, ai.dst[ai.wnd_to_change].width, ai.dst[ai.wnd_to_change].height);
    XMoveResizeWindow (g_dpy, ai.draws[ai.wnd_to_change], ai.dst[ai.wnd_to_change].x, ai.dst[ai.wnd_to_change].y,
                       ai.dst[ai.wnd_to_change].width, ai.dst[ai.wnd_to_change].height);

    XFlush (g_dpy);
}

static void
hwc_unset (void)
{

}

static void
usage (char *argv)
{
    printf ("%s usage : \n", argv);
    printf ("   %s (options) (parameters)\n", argv);
    printf ("  (options) :\n");
    printf ("   -set    [wnd_ids]        : set external window list for screen configuration\n");
    printf ("   -setw   [num_of_wnd]     : set the inner window list for screen configuration\n");
    printf ("   -setp   [num_of_pix]     : set the inner pixmap list for the screen configuration\n");
    printf ("   -setwr  [num_of_wnd]     : set the inner window list for screen configuration (use root)\n");
    printf ("   -setpr  [num_of_pix]     : set the inner pixmap list for the screen configuration (use root)\n");
    printf ("   -move   windows_position_to_change x y        : TODO\n");
    printf ("   -loop                    : event_loop test\n");
    printf ("   -redir                   : ask X server to redirect windows.\n");

    printf ("\n");
    printf ("  examples :\n");
    printf ("   %s -set 0x60000a 0x60000b : hwc set two windows\n", argv);
    printf ("   %s -redir -set 0x20000a : hwc set one redirected window\n", argv);
    printf ("   %s -set             : app attempts to set all windows in X window's hierarchy\n", argv);
    printf ("   %s -setw 3          : hwc set 3 windows\n", argv);
    printf ("   %s -setp 4          : hwc set 4 pixmaps\n", argv);
    printf ("   %s -setwr 3 -redir  : hwc set 3 redirected windows\n", argv);
    printf ("   %s -loop             \n", argv);
}

static int
check_options (int argc, char *argv[])
{
    int i = 1;
    int j = 0;

    if (argc < 2) goto fail;

    // check if we want to redirect windows
    for (j = 1; j < argc; j++)
    {
        if (!strcmp (argv[j], "-redir")) ai.redirect = 1;
    }

    // check if we want to set external windows
    for (j = 1; j < argc; j++)
    {
        if (!strcmp (argv[j], "-set") || !strcmp (argv[j], "-setr"))
        {
            if (!strcmp (argv[j], "-setr")) ai.use_root = 1;

            // if we have 'hwc-sample -set'
            if (j + 1 >= argc)
                ai.set_all_ext_wnds = 1; // set all external windows

            // if we have 'hwc-sample -set some_win_ids'
            // we simple store all specified numbers (in hex), after '-set' key, as
            // a window's ids in allocated array (ai.ext_wnd_ids[])
            else
            {
                ai.set_spec_ext_wnds = 1; // set specified external windows
                ai.num_of_ext_wnds = argc - j - 1;

                ai.ext_wnd_ids = (Window*) calloc (ai.num_of_ext_wnds, sizeof(Window));

                while (++j < argc)
                    sscanf (argv[j], "%lx", ai.ext_wnd_ids++);

                ai.ext_wnd_ids -= ai.num_of_ext_wnds;

                for (j = 0; j < ai.num_of_ext_wnds; j++)
                    printf ("  wnd's id: 0x%06lx.\n", ai.ext_wnd_ids[j]);
            }

            ai.hwc_set = 2; // because this windows too

            // this mode doesn't support any other keys, besides '-redir'
            return 1;
        }
    }

    if (!strcmp (argv[i], "-help")) goto fail;

    if (!strcmp (argv[i], "-loop"))
    {
        ai.hwc_set = 3;
        return 1;
    }

    if (!strcmp (argv[i], "-setp") || !strcmp (argv[i], "-setpr"))
    {
        if (!strcmp (argv[i], "-setpr")) ai.use_root = 1;
        if (argc < 3) goto fail;

        make_pixmaps (atoi (argv[2]));
        ai.hwc_set = 1;
        printf (" _hwset enabled_ -setp \n");

        return 1;
    }

    if (!strcmp (argv[i], "-move"))
    {
        /* -move 2 x y*/
        if (argc < 5) goto fail;
        int pos = 0;
        pos = atoi (argv[2]);
        if (pos > (AMOUNT_OF_PLANES - 1)) goto fail;

        ai.dst[pos].x = atoi (argv[3]);
        ai.dst[pos].y = atoi (argv[4]);
        ai.wnd_to_change = pos;
        ai.hwc_set = 4;
        printf (" _hwset enabled_ -movew \n");

        return 1;
    }

    if (!strcmp (argv[i], "-setw") || !strcmp (argv[i], "-setwr"))
    {
        if (!strcmp (argv[i], "-setwr")) ai.use_root = 1;

        if (argc < 3) goto fail;

        make_windows (atoi (argv[2]));

        ai.hwc_set = 2;
        return 1;
    }

fail:
    usage (argv[0]);
    return 0;
}

void
change_focus (void)
{
    int i;
    Drawable tP;
    XRectangle tD;
    XRectangle tS;
    for (i = ai.use_root; i < ai.count - 1; i++)
    {
        tP = ai.draws[i];
        tD = ai.dst[i];
        tS = ai.src[i];

        ai.draws[i] = ai.draws[i + 1];
        ai.src[i] = ai.src[i + 1];
        ai.dst[i] = ai.dst[i + 1];

        ai.draws[i + 1] = tP;
        ai.src[i + 1] = tS;
        ai.dst[i + 1] = tD;
    }
}

// preparing X Composition extension to use
//==================================================================
int
compositing_prepare (void)
{
    int res = 0;
    int major_v = 0, minor_v = 4;
    int compos_event, compos_error;

    res = XCompositeQueryExtension (g_dpy, &compos_event, &compos_error);
    if (!res)
    {
        printf ("No Composite extension is.\n");
        return -1;
    }

    XCompositeQueryVersion (g_dpy, &major_v, &minor_v);
    printf ("X Composite extension version: %d.%d\n", major_v, minor_v);

    return 0;
}

//
//==================================================================
int
hwc_prepare (void)
{
    int major, minor;
    int maxLayer;

    printf ("XQueryExtension for HWC support.\n");
    if (!XQueryExtension (g_dpy, HWC_NAME, &hwc_op_code, &hwc_event, &hwc_error))
    {
        fprintf ( stderr, "HWC NOT support !!!\n");
        return 0;
    }

    printf ("HWCQueryVersion\n");
    if (!HWCQueryVersion (g_dpy, &major, &minor))
    {
        printf ("Error: HWCQueryVerion failed.\n");
        return 0;
    }

    printf ("HWC extension version: %d.%d\n", major, minor);

    printf ("HWCOpen\n");
    if (!HWCOpen (g_dpy, 0, &maxLayer))
    {
        printf ("Error: HWCOpen failed.\n");
        return 0;
    }

    printf ("HWC layer number: %d.\n", maxLayer);

    printf ("HWCSelectInput\n");
    HWCSelectInput (g_dpy, root, HWCAllEvents);

    return maxLayer;
}

GC
create_gc (void)
{
    XFontStruct *fontinfo;
    XGCValues gr_values;
    GC gc = NULL;

    fontinfo = XLoadQueryFont (g_dpy, "8x10");
    if (!fontinfo) fontinfo = XLoadQueryFont (g_dpy, "9x10");
    if (!fontinfo) fontinfo = XLoadQueryFont (g_dpy, "fixed");
    if (fontinfo) gr_values.font = fontinfo->fid;

    gr_values.function = GXcopy;
    gr_values.plane_mask = AllPlanes;
    //    gr_values.background = BlackPixel(g_dpy,DefaultScreen (g_dpy));
    gr_values.foreground = BlackPixel(g_dpy, DefaultScreen (g_dpy));
    gc = XCreateGC (g_dpy, root,
    GCFont | GCFunction | GCPlaneMask | GCForeground | GCBackground,
                    &gr_values);

    XSetBackground (g_dpy, gc, 0xFFFFFFFF); //0xFF000000);

    XSync (g_dpy, False);

    return gc;
}

void
allocate_colors (XColor* red, XColor* brown, XColor* blue)
{
    int rc = 0;

    Colormap screen_colormap; /* color map to use for allocating colors.   */

    /* get access to the screen's color map. */
    screen_colormap = DefaultColormap(g_dpy, DefaultScreen(g_dpy));

    /* allocate the set of colors we will want to use for the drawing. */
    rc = XAllocNamedColor (g_dpy, screen_colormap, "red", red, red);
    if (rc == 0)
    {
        fprintf (stderr, "XAllocNamedColor - failed to allocated 'red' color.\n");
        exit (1);
    }
    rc = XAllocNamedColor (g_dpy, screen_colormap, "brown", brown, brown);
    if (rc == 0)
    {
        fprintf (stderr, "XAllocNamedColor - failed to allocated 'brown' color.\n");
        exit (1);
    }
    rc = XAllocNamedColor (g_dpy, screen_colormap, "blue", blue, blue);
    if (rc == 0)
    {
        fprintf (stderr, "XAllocNamedColor - failed to allocated 'blue' color.\n");
        exit (1);
    }
}

//
//==================================================================
void
create_control_buttons (void)
{
    change_size = XCreateSimpleWindow (g_dpy, root, dispsize_width - 110, dispsize_height - 110, wd1, ht1, 0, 0,
                                       0x0000FFFF);
    move_btn = XCreateSimpleWindow (g_dpy, root, dispsize_width - 2 * 110, dispsize_height - 110, wd1, ht1, 0, 0,
                                    0x000001FF);
    change_stack_order = XCreateSimpleWindow (g_dpy, root, dispsize_width - 3 * 110, dispsize_height - 110, wd1, ht1, 0,
                                              0, red.pixel);

    XMapWindow (g_dpy, change_size);
    XMapWindow (g_dpy, move_btn);
    XMapWindow (g_dpy, change_stack_order);

    XSelectInput (g_dpy, change_size, ButtonPressMask);
    XSelectInput (g_dpy, move_btn, ButtonPressMask);
    XSelectInput (g_dpy, change_stack_order, ButtonPressMask);
}

// return geometry of specified, by win_id, window
//==================================================================
XRectangle
get_wnd_geometry (Window win_id)
{
    XRectangle geometry;
    XWindowAttributes win_attr;

    XGetWindowAttributes (g_dpy, win_id, &win_attr);

    geometry.x = win_attr.x;
    geometry.y = win_attr.y;
    geometry.width = win_attr.width;
    geometry.height = win_attr.height;

    return geometry;
}

// fill ai structure by using information about external windows
// ask X server to give us messages, which will be generated to external
// windows (for interception them)
//==================================================================
int
grab_external_wnds (void)
{
    int i;
    XRectangle tmp_rect;

    // set all external windows
    if (ai.set_all_ext_wnds)
    {
        printf ("\ngrab all external windows:\n");
        find_all_window_ids ();

        // we can supply up to (AMOUNT_OF_PLANES - 1) windows only
        ai.count = nchildrens <= (AMOUNT_OF_PLANES - 1) ? nchildrens : (AMOUNT_OF_PLANES - 1);

        if (!ai.count)
        {
            printf ("no windows to manipulate.\n");
            return 1;
        }

        // set root window on bottom layer
        ai.draws[0] = XDefaultRootWindow (g_dpy);

        ai.src[0].x = 0;
        ai.src[0].y = 0;
        ai.src[0].width = dispsize_width;
        ai.src[0].height = dispsize_height;

        ai.dst[0].x = 0;
        ai.dst[0].y = 0;
        ai.dst[0].width = dispsize_width;
        ai.dst[0].height = dispsize_height;

        ai.count++;
        ai.use_root = 1;

        printf (" root window 0x%06lx has been gripped.\n", ai.draws[0]);

        // iterate over all windows
        for (i = 1; i < ai.count; i++)
        {
            ai.draws[i] = root_childrens[i - 1];

            // dst - this is geometry relative to physical screen
            tmp_rect = get_wnd_geometry (ai.draws[i]);
            ai.dst[i] = tmp_rect;

            // src - this is geometry relative to bo
            tmp_rect.x = 0;
            tmp_rect.y = 0;
            ai.src[i] = tmp_rect;

            printf (" external window 0x%06lx has been gripped.\n", ai.draws[i]);
        }
        printf ("\n");
    }

    // set specified external windows
    else if (ai.set_spec_ext_wnds)
    {
        printf ("\ngrab specified external windows:\n");

        ai.count = ai.num_of_ext_wnds;

        // hwc will set root window on a bottom layer
        if (ai.use_root)
        {
            ai.draws[0] = XDefaultRootWindow (g_dpy);

            ai.src[0].x = 0;
            ai.src[0].y = 0;
            ai.src[0].width = dispsize_width;
            ai.src[0].height = dispsize_height;

            ai.dst[0].x = 0;
            ai.dst[0].y = 0;
            ai.dst[0].width = dispsize_width;
            ai.dst[0].height = dispsize_height;

            ai.count++;

            printf (" root window 0x%06lx has been gripped.\n", ai.draws[0]);
        }

        // iterate over specified windows, take into account root window
        for (i = ai.use_root; i < ai.count && i < AMOUNT_OF_PLANES; i++)
        {
            // for correct access to ext_wnd_ids[] array,
            // we have gripped root window in ai.draws[0]
            if (ai.use_root)
                ai.draws[i] = ai.ext_wnd_ids[i - 1];
            else
                ai.draws[i] = ai.ext_wnd_ids[i];

            // dst - this is geometry relative to physical screen
            tmp_rect = get_wnd_geometry (ai.draws[i]);
            ai.dst[i] = tmp_rect;

            // src - this is geometry relative to bo
            tmp_rect.x = 0;
            tmp_rect.y = 0;
            ai.src[i] = tmp_rect;

            printf (" external window 0x%06lx has been gripped.\n", ai.draws[i]);
        }
        printf ("\n");
    }

    return 0;
}

// find all windows, which are root window's childrens and have names, and
// print information about them
//========================================================================
int
find_all_window_ids (void)
{
    int i;
    Window root_return;
    Window parent_return;
    char* window_name = NULL;

    int j;
    Window* tmp = NULL;

    // find all windows, which are childrens of root window and store their
    // id's in 'tmp[] array'
    if (!XQueryTree (g_dpy, RootWindow(g_dpy, screen), &root_return, &parent_return, &tmp, &nchildrens))
    {
        printf ("error while XQueryTree call.\n");
        return 1;
    }

    // calculate amount of named windows
    for (i = 0, j = 0; i < nchildrens; i++)
    {
        if (XFetchName (g_dpy, tmp[i], &window_name))
        {
            j++;
            XFree ((void*) window_name);
        }
    }

    root_childrens = calloc (j, sizeof(Window));

    // create array root_childrens[] which consists of wnd's id to named windows only
    for (i = 0, j = 0; i < nchildrens; i++)
    {
        if (XFetchName (g_dpy, tmp[i], &window_name))
        {
            root_childrens[j++] = tmp[i];
            XFree ((void*) window_name);
        }
    }

    nchildrens = j;

    printf (" RootWindow: 0x%06lx.\n", RootWindow(g_dpy, screen));
    printf (" root_return: 0x%06lx.\n", root_return);
    printf (" parent_return: 0x%06lx.\n", parent_return);
    printf (" nchildren_return: %d.\n", nchildrens);

    printf ("\nnumber    window's id    window's name\n");
    printf ("     0       0x%06lx        root\n", RootWindow(g_dpy, screen));

    for (i = 0; i < nchildrens && i < (AMOUNT_OF_PLANES - 1); i++)
    {
        printf ("     %d ", i + 1);
        printf ("      0x%06lx ", root_childrens[i]);

        XFetchName (g_dpy, root_childrens[i], &window_name);
        printf ("       %s\n", window_name);
        XFree ((void*) window_name);
    }
    printf ("\n\n");

    XFlush (g_dpy);

    return 0;
}

// ask X server to redirect windows
//========================================================================
void
redirect_wnds (void)
{
    int i;

    // if we don't work with windows
    if (ai.hwc_set != 2) return;

    for (i = ai.use_root; i < ai.count; i++)
    {
        //Pixmap temp;
        XCompositeRedirectWindow (g_dpy, (Window) ai.draws[i], CompositeRedirectManual);
        printf (" window 0x%06lx has been redirected to off-screen storage.\n", (Window) ai.draws[i]);

        //temp = XCompositeNameWindowPixmap( g_dpy, (Window)ai.draws[i] );

        XMapWindow (g_dpy, (Window) ai.draws[i]);
        //ai.draws[i] = temp;
    }

    //XCompositeGetOverlayWindow( g_dpy, )
    XFlush (g_dpy);

    printf ("\n");
}

// use HWC to handle reconfig requests from other clients (move&change size&stacking)
//==================================================================
void
reconfig_handler (XEvent* event, int request)
{
    int i = 0;
    int flag = 0;

    if (request == ConfigureRequest)
    {
#ifdef _DEBUG
        printf ("---------------------------------------------\n");
        printf ("ConfigureRequest on wnd: 0x%06lx (parent: 0x%06lx).\n\
                  request to change window to %d, %d [%dx%d].\n\
                  value mask: %ld.\n\n",
                event->xconfigurerequest.window, event->xconfigurerequest.parent, event->xconfigurerequest.x,
                event->xconfigurerequest.y, event->xconfigurerequest.width, event->xconfigurerequest.height,
                event->xconfigurerequest.value_mask);
#endif
        for (i = ai.use_root; i < ai.count; i++)
        {
            // if we have gripped external windows with id, for which
            // we have request to change
            if (ai.draws[i] == event->xconfigurerequest.window)
            {
                flag = 1;
                ai.dst[i].x = event->xconfigurerequest.x;
                ai.dst[i].y = event->xconfigurerequest.y;
                ai.dst[i].width = event->xconfigurerequest.width;
                ai.dst[i].height = event->xconfigurerequest.height;
                ai.src[i].width = event->xconfigurerequest.width;
                ai.src[i].height = event->xconfigurerequest.height;

                XMoveResizeWindow (g_dpy, ai.draws[i], ai.dst[i].x, ai.dst[i].y, ai.dst[i].width, ai.dst[i].height);
                XFlush (g_dpy);

                break;
            }
        }

        // reconfigure windows through HWC
        if (flag) hwc_set ();

    }
    else if (request == ResizeRequest)
    {
#ifdef _DEBUG
        printf ("ResizeRequest on wnd: 0x%06lx.\n\
                  ask to change window size to %dx%d.\n",
                event->xresizerequest.window, event->xresizerequest.width, event->xresizerequest.height);
#endif
    }
}

int
main (int argc, char *argv[])
{
    int i = 0;
    int res;

    GC gc;

    memset (&ai, 0x0, sizeof(struct app_info));

    g_dpy = XOpenDisplay (NULL);
    if (!g_dpy)
    {
        printf ("Error : fail to open display\n");
        return 1;
    }

    root = DefaultRootWindow(g_dpy);
    screen = DefaultScreen(g_dpy);
    depth = DefaultDepth(g_dpy, screen);

    compositing_prepare ();
    ai.maxLayer = hwc_prepare ();

    if (!ai.maxLayer)
    {
        printf ("No available planes are.\n");
        goto fail;
    }

    // within it function windows or pixmaps will be created
    if (!check_options (argc, argv)) goto fail;

    // grab external windows, if aren't any windows - exit
    res = grab_external_wnds ();
    if (res) goto fail;

    // in this point we have created window (external or internal) or pixmap,
    // picked work mode (ai.hwc_set), filled ai structure to HWCSetDrawables call
    // and decision whether we want to do redirect or not.

    if (ai.redirect) redirect_wnds ();

    gc = create_gc ();

    switch (ai.hwc_set)
    {
        case 1:
        case 2:
            hwc_set ();
            sprintf (str1, "Set %s Done!", ((ai.hwc_set == 1) ? "Pixmaps" : "Windows"));
            len1 = strlen (str1);
            //XDrawString (g_dpy, root, gc, 10, 10, str1, len1);

            break;
        case 3:
            hwc_loop ();

            strcpy (str1, "Hwc Loop start!");
            len1 = strlen (str1);
            XDrawString (g_dpy, root, gc, 10, 10, str1, len1);
            break;
        case 4:
            hwc_movew ();
            strcpy (str1, "Move Window Done!");
            len1 = strlen (str1);
            XDrawString (g_dpy, root, gc, 10, 10, str1, len1);

            break;
        case 0:
        default:
            hwc_unset ();
    }

    XFlush (g_dpy);

    allocate_colors (&red, &brown, &blue);
    create_control_buttons ();

    // this call must be done after any windows create call !!!
    // ask X server to send us messages ConfigureRequest and/or ResizeRequest, when another
    // X11 clients have attempt to reconfigure windows, which are root's childrens
    XSelectInput (g_dpy, root, ResizeRedirectMask | SubstructureRedirectMask);

    // some draw operations with control-windows (such a buttons)
    XFillRectangle (g_dpy, change_size, gc, wd1 / 2 - 30, wd1 / 2 - 10, 60, 20);
    XDrawImageString (g_dpy, change_size, gc, 10, 15, CHANGE_SIZE_WND, strlen (CHANGE_SIZE_WND));

    XFillRectangle (g_dpy, move_btn, gc, wd1 / 2 - 30, wd1 / 2 - 10, 60, 20);
    XDrawImageString (g_dpy, move_btn, gc, 10, 15, MOVE_WND, strlen (MOVE_WND));

    XFillRectangle (g_dpy, change_stack_order, gc, wd1 / 2 - 30, wd1 / 2 - 10, 60, 20);
    XDrawImageString (g_dpy, change_stack_order, gc, 0, 15, CHANGE_STACK_ORDER_WND, strlen (CHANGE_STACK_ORDER_WND));

    XFlush (g_dpy);

    // create threads that will be responsible of stdin's work control
    if ((ai.hwc_set > 0) && (ai.hwc_set < 6))
    {
        create_hwcthread (&thread_data);
    }

    // X event loop --------------------
    XEvent event;

    while (True)
    {
        XNextEvent (g_dpy, &event);/* wating for x event */
        switch (event.type)
        {
            case ButtonPress:
                if (event.xbutton.window == change_size)
                {
                    /* initialize random seed: */
                    int pos = rand () % ai.count;
                    if (pos == 0 && ai.use_root) pos = 1;

                    ai.dst[pos].width += 10;
                    ai.dst[pos].height += 20;
                    ai.src[pos].width = ai.dst[pos].width;
                    ai.src[pos].height = ai.dst[pos].height;
                    ai.wnd_to_change = pos;
                    ai.hwc_set = 5;

                    hwc_resizew ();
                    hwc_set ();
                }
                else if (event.xbutton.window == move_btn)
                {
                    /* initialize random seed: */
                    int pos = rand () % ai.count;
                    if (pos == 0 && ai.use_root) pos = 1;

                    ai.dst[pos].x += 10;
                    ai.dst[pos].y += 50;
                    ai.wnd_to_change = pos;
                    ai.hwc_set = 4;

                    hwc_movew ();
                    hwc_set ();
                }
                else if (event.xbutton.window == change_stack_order)
                {
                    change_focus ();
                    hwc_set ();
                }

                break;

                // this event will be sent, by Xserver, when some other clients try
                // to reconfigure root's children windows
            case ConfigureRequest:
                reconfig_handler (&event, ConfigureRequest);
                break;

            case ResizeRequest:
                reconfig_handler (&event, ResizeRequest);
                break;

            default:
                break;

        }
    }

    for (i = 0; i < ai.count; i++)
    {
        if (ai.draws[i]) XFreePixmap (g_dpy, ai.draws[i]);
        if (ai.draws[i]) XDestroyWindow (g_dpy, ai.draws[i]);

    }

fail:
    XCloseDisplay (g_dpy);
    return 1;
}

