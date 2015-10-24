/**************************************************************************

 square_bubbles

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

// This file implements window's manipulation like side-bump bubbles
// through Xlib. This app can be used with hwc-sample to pretty view of HWC work.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <math.h>
#include <fcntl.h>

#include <X11/X.h>
#include <X11/Xlib.h>

#define _DEBUG 1
#define MAX_WNDS (3)    // apart root window
#define SCREEN_WIDTH (720)
#define SCREEN_HEIGHT (1280)
#define ODD_INTERVAL (10) // interval of wnd's size odd changing (in seconds)
#define RESIZE_SPEED (0.8) // radians per second

typedef enum Direct_t
{
    UP_RIGHT,
    DOWN_RIGHT,
    UP_LEFT,
    DOWN_LEFT
} Direct;

typedef struct bubbles_t
{
    XRectangle curr_geometry;
    XRectangle first_geometry;
    u_int32_t x_range;
    u_int32_t y_range;
    Window id;
    Direct direct;
    double angle;
} bubbles;

// Xsever specific variables
Display* dpy;
Window root;
int depth;
int screen;

// information about all external windows (with names)
Window* root_childrens;
unsigned int nchildrens;

bubbles square_bubbles[MAX_WNDS];
unsigned int real_cnt;

// we can only close all windows by pass '-cls' to this app
int cls;         // whether we need to close all windows or not

int move;        // whether we need to move all windows or not
int change_size; // whether we need to change size of all windows or not
int delay = 50; // delay between bubbles's position&size calculation in ms

int debug = 1;      // whether we want to print or not

int all_wnds = 1;   // whether we want to manipulate all windows or not
int odd_size;       // whether we want to use odd sizes of windows

void cmd_parse (int argc, char* argv[]);
int find_all_window_ids (void);
XRectangle get_wnd_geometry (Window win_id);
void shift_wnd (bubbles* s_b);

// this function returns randomized 32 bit unsigned value
// in order to bypass prevent (static analizator) warning (don't use rand() function)
//========================================================================
u_int32_t
hand_made_rand (void)
{
    int ret, fd;
    u_int32_t rand_number;

    fd = open ("/dev/urandom", O_RDONLY);
    if (fd < 0)
        return 11;   // why 11, why not?

    // simple read 4 random bytes from /dev/urandom
    ret = read (fd, &rand_number, sizeof(u_int32_t));
    if (ret < 0 || ret != sizeof(rand_number))
        rand_number = 11;   // why 11, why not?

    close (fd);

    return rand_number;
}

//========================================================================
int
main (int argc, char* argv[])
{
    int i;

    cmd_parse (argc, argv);

    if (!change_size && !move)
    {
        printf ("nothing to do, specify what you want (i.e: square_bubbles -m -s).\n");
        return 1;
    }

    // delay is passed in ms, but usleep requires delay in mcs
    delay *= 1000;

    dpy = XOpenDisplay ( NULL);

    if (!dpy)
    {
        printf ("error while XOpenDisplay call.\n");
        return 1;
    }

    root = DefaultRootWindow(dpy);
    screen = DefaultScreen(dpy);
    depth = DefaultDepth(dpy, screen);

    if (all_wnds)
    {
        // allocate memory for root_childrens[] array
        if (find_all_window_ids ())
        {
            XCloseDisplay (dpy);
            return 0;
        }
    }

    // we can supply up to MAX_WNDS windows only
    real_cnt = nchildrens <= MAX_WNDS ? nchildrens : MAX_WNDS;

    if (!real_cnt)
    {
        printf ("no windows to manipulate.\n");
        XCloseDisplay (dpy);
        return 0;
    }

    // prior initialization
    for (i = 0; i < real_cnt; i++)
    {
        square_bubbles[i].id = root_childrens[i];
        square_bubbles[i].curr_geometry = get_wnd_geometry (root_childrens[i]);
        square_bubbles[i].direct = hand_made_rand () % 4;

        memcpy (&square_bubbles[i].first_geometry, &square_bubbles[i].curr_geometry,
                sizeof(square_bubbles[i].first_geometry));
        square_bubbles[i].x_range = square_bubbles[i].first_geometry.width / 4;
        square_bubbles[i].y_range = square_bubbles[i].first_geometry.height / 4;
    }

    while (1)
    {
        for (i = 0; i < real_cnt; i++)
        {
            shift_wnd (&square_bubbles[i]);

            XMoveResizeWindow (dpy, square_bubbles[i].id, square_bubbles[i].curr_geometry.x,
                               square_bubbles[i].curr_geometry.y, square_bubbles[i].curr_geometry.width,
                               square_bubbles[i].curr_geometry.height);

#ifdef _DEBUG
            printf (" move and resize wnd (id: 0x%06lx) to %d,%d [%dx%d].\n", square_bubbles[i].id,
                    square_bubbles[i].curr_geometry.x, square_bubbles[i].curr_geometry.y,
                    square_bubbles[i].curr_geometry.width, square_bubbles[i].curr_geometry.height);
#endif
        }
        XFlush (dpy);
        usleep (delay);
    }

    if (all_wnds)
        XFree ((void*) root_childrens);
    else
        free ((void*) root_childrens);

    XCloseDisplay (dpy);

    return 0;
}

// parse command line
//========================================================================
void
cmd_parse (int argc, char* argv[])
{
    int i;

    if (argc < 2) return;

    // if we want to obtain help
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-help"))
        {
            printf ("usage:\n");
            printf ("  -help  : print this help.\n");
            printf ("  -cls   : close all root's children windows.\n");
            printf ("  -m     : move bubbles.\n");
            printf ("  -s     : change bubbles's size.\n");
            printf ("  -delay : delay between bubbles's position&size calculation in ms.\n");
            printf ("  -odd   : use window's odd sizes.\n");
            printf ("\n");
            break;
        }
    }

    // if we want to close all windows
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-cls"))
        {
            cls = 1;
            return;
        }
    }

    // if we want to move windows
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-m"))
        {
            move = 1;
            break;
        }
    }

    // if we want to change windows's size
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-s"))
        {
            change_size = 1;
            break;
        }
    }

    // if we want to change delay in ms
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-delay"))
        {
            if (i + 1 >= argc)
                printf ("error while argument parsing. i.e: square_bubbles -m -delay 30\n");
            else
                sscanf (argv[i + 1], "%d", &delay);
            break;
        }
    }

    // check if we want to set specified windows
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-set"))
        {
            // if we have 'square_bubbles -set'
            if (i + 1 >= argc)
            {
                all_wnds = 1; // set all windows
                printf ("all named windows will be used.\n");
            }

            // if we have 'square_bubbles -set wnd_ids'
            // we simple store all specified numbers (in hex), after '-set' key, as
            // a window's ids in allocated array (root_childrens[])
            else
            {
                all_wnds = 0; // set specified windows
                nchildrens = argc - i - 1;

                root_childrens = (Window*) calloc (nchildrens, sizeof(Window));

                while (++i < argc)
                    sscanf (argv[i], "%lx", root_childrens++);

                root_childrens -= nchildrens;

                printf ("these named windows will be used:\n");

                for (i = 0; i < nchildrens; i++)
                    printf ("  wnd's id: 0x%06lx.\n", root_childrens[i]);
            }

            break;
        }
    }

    // if we want to use even/odd size switch
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-odd"))
        {
            odd_size = 1;
            break;
        }
    }
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
    if (!XQueryTree (dpy, RootWindow(dpy, screen), &root_return, &parent_return, &tmp, &nchildrens))
    {
        printf ("error while first XQueryTree call.\n");
        return 1;
    }

    if (cls)
    {
        // destroy all windows
        for (i = 0; i < nchildrens; i++)
            XDestroyWindow (dpy, tmp[i]);

        XFree ((void*) tmp);
        return 1;
    }

    // calculate amount of named windows
    for (i = 0, j = 0; i < nchildrens; i++)
    {
        if (XFetchName (dpy, tmp[i], &window_name))
        {
            j++;
            XFree ((void*) window_name);
        }
    }

    root_childrens = calloc (j, sizeof(Window));

    // create array root_childrens[] which consists of wnd's id to named windows only
    for (i = 0, j = 0; i < nchildrens; i++)
    {
        if (XFetchName (dpy, tmp[i], &window_name))
        {
            root_childrens[j++] = tmp[i];
            XFree ((void*) window_name);
        }
    }

    nchildrens = j;

    printf (" RootWindow: 0x%06lx.\n", RootWindow(dpy, screen));
    printf (" root_return: 0x%06lx.\n", root_return);
    printf (" parent_return: 0x%06lx.\n", parent_return);
    printf (" nchildren_return: %d.\n", nchildrens);

    printf ("\nnumber    window's id    window's name\n");

    for (i = 0; i < nchildrens && i < MAX_WNDS; i++)
    {
        printf ("     %d ", i);
        printf ("      0x%06lx ", root_childrens[i]);

        XFetchName (dpy, root_childrens[i], &window_name);
        printf ("       %s\n", window_name);
        XFree ((void*) window_name);
    }
    printf ("\n\n");

    XFlush (dpy);

    return 0;
}

// return geometry of specified, by win_id, window
//========================================================================
XRectangle
get_wnd_geometry (Window win_id)
{
    XRectangle geometry;
    XWindowAttributes win_attr;

    XGetWindowAttributes (dpy, win_id, &win_attr);

    geometry.x = win_attr.x;
    geometry.y = win_attr.y;
    geometry.width = win_attr.width;
    geometry.height = win_attr.height;

    return geometry;
}

// calculate new coordinates
//========================================================================
void
shift_wnd (bubbles* s_b)
{
    short x_step = 5;
    short y_step = 5;
    int width;
    int height;

    // each ODD_INTERVAL seconds we change wnd's size odd mode
    int up_edge = ODD_INTERVAL * 1E6 / delay;
    static int cnt_odd;
    static int even = 1;

    if (!s_b) return;

    if (change_size)
    {
        width  = s_b->x_range * sin (s_b->angle) + s_b->first_geometry.width;
        height = s_b->y_range * sin (s_b->angle) + s_b->first_geometry.height;

        // if we want to use odd size of windows (first condition)
        if ((odd_size) && (cnt_odd++ >= up_edge))
        {
            cnt_odd = 0;
            even ^= 0x01;
            printf ("even/odd wnd's size changed.\n");
        }

        // make even w&h
        if (even)
        {
            width &= ~0x01;
            height &= ~0x01;
        }
        // make odd w&h (w&h values is checked in followed code)
        else
        {
            width |= 0x01;
            height |= 0x01;
        }

        if ((s_b->curr_geometry.x + width <= (SCREEN_WIDTH - 1))
                && (s_b->curr_geometry.y + s_b->curr_geometry.height <= (SCREEN_HEIGHT - 1)))
        {
            s_b->curr_geometry.width = width;
            s_b->curr_geometry.height = height;
            s_b->angle += RESIZE_SPEED * delay * 1E-6;
        }
    }

    if (!move) return;

    switch (s_b->direct)
    {
        case UP_RIGHT:
            s_b->curr_geometry.x += x_step;
            s_b->curr_geometry.y -= y_step;

            // if bubble has striked in up right corner,
            // it will be pushed to up_left direct

            // right side bump
            if (s_b->curr_geometry.x + s_b->curr_geometry.width > (SCREEN_WIDTH - 1))
            {
                if (s_b->curr_geometry.y <= 0)
                {
                    printf ("UP_RIGHT: right side bump\n");
                    s_b->curr_geometry.y = y_step;
                }

                s_b->direct = UP_LEFT;

                s_b->curr_geometry.x -= x_step;
                s_b->curr_geometry.y -= y_step;
            }

            // top side bump
            else if (s_b->curr_geometry.y < 0)
            {
                if (s_b->curr_geometry.x + s_b->curr_geometry.width >= (SCREEN_WIDTH - 1))
                {
                    printf ("UP_RIGHT: top side bump\n");
                    s_b->curr_geometry.x -= x_step;
                }

                s_b->direct = DOWN_RIGHT;

                s_b->curr_geometry.x += x_step;
                s_b->curr_geometry.y += y_step;
            }
            break;

        case DOWN_RIGHT:
            s_b->curr_geometry.x += x_step;
            s_b->curr_geometry.y += y_step;

            // if bubble has striked in bottom right corner,
            // it will be pushed to down_left direct

            // right side bump
            if (s_b->curr_geometry.x + s_b->curr_geometry.width > (SCREEN_WIDTH - 1))
            {
                if (s_b->curr_geometry.y + s_b->curr_geometry.height >= (SCREEN_HEIGHT - 1))
                {
                    printf ("DOWN_RIGHT: right side bump\n");
                    s_b->curr_geometry.y = (SCREEN_HEIGHT - 1) - s_b->curr_geometry.height - y_step;
                }

                s_b->direct = DOWN_LEFT;

                s_b->curr_geometry.x -= x_step;
                s_b->curr_geometry.y += y_step;
            }

            // bottom side bump
            else if (s_b->curr_geometry.y + s_b->curr_geometry.height > (SCREEN_HEIGHT - 1))
            {
                if (s_b->curr_geometry.x + s_b->curr_geometry.width >= (SCREEN_WIDTH - 1))
                {
                    printf ("DOWN_RIGHT: bottom side bump\n");
                    s_b->curr_geometry.x -= x_step;
                }

                s_b->direct = UP_RIGHT;

                s_b->curr_geometry.x += x_step;
                s_b->curr_geometry.y -= y_step;
            }
            break;

        case UP_LEFT:
            s_b->curr_geometry.x -= x_step;
            s_b->curr_geometry.y -= y_step;

            // if bubble has striked in top left corner,
            // it will be pushed to up_right direct

            // left side bump
            if (s_b->curr_geometry.x < 0)
            {
                if (s_b->curr_geometry.y <= 0)
                {
                    printf ("UP_LEFT: left side bump\n");
                    s_b->curr_geometry.y = y_step;
                }

                s_b->direct = UP_RIGHT;

                s_b->curr_geometry.x += x_step;
                s_b->curr_geometry.y -= y_step;
            }

            // top side bump
            else if (s_b->curr_geometry.y < 0)
            {
                if (s_b->curr_geometry.x <= 0)
                {
                    printf ("UP_LEFT: top side bump\n");
                    s_b->curr_geometry.x = x_step;
                }

                s_b->direct = DOWN_LEFT;

                s_b->curr_geometry.x -= x_step;
                s_b->curr_geometry.y += y_step;
            }
            break;

        case DOWN_LEFT:
            s_b->curr_geometry.x -= x_step;
            s_b->curr_geometry.y += y_step;

            // if bubble has striked in bottom left corner,
            // it will be pushed to down_right direct

            // left side bump
            if (s_b->curr_geometry.x < 0)
            {
                if (s_b->curr_geometry.y + s_b->curr_geometry.height >= (SCREEN_HEIGHT - 1))
                {
                    printf ("DOWN_LEFT: left side bump\n");
                    s_b->curr_geometry.y = (SCREEN_HEIGHT - 1) - s_b->curr_geometry.height - y_step;
                }

                s_b->direct = DOWN_RIGHT;

                s_b->curr_geometry.x += x_step;
                s_b->curr_geometry.y += y_step;
            }

            // bottom side bump
            else if (s_b->curr_geometry.y + s_b->curr_geometry.height > (SCREEN_HEIGHT - 1))
            {
                if (s_b->curr_geometry.x <= 0)
                {
                    printf ("DOWN_LEFT: bottom side bump\n");
                    s_b->curr_geometry.x = x_step;
                }

                s_b->direct = UP_LEFT;

                s_b->curr_geometry.x -= x_step;
                s_b->curr_geometry.y -= y_step;
            }
            break;

        default:
            printf (" no correct direction.\n");
            break;
    }

    if (s_b->curr_geometry.x < 0 || s_b->curr_geometry.y < 0)
    {
        printf ("error with calculation algorithm.\n");
        exit (1);
    }
}
