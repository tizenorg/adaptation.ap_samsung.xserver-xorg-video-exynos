#include <stdio.h>
#include <string.h>
#include <xcb/xcb.h>

#include "dri2_dri3.h"

extern xcb_rectangle_t wnd_pos;
extern int mode;
extern uint32_t present_bufs;

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

    // if we want to set rotate speed.
    for (i = 1; i < argc; i++)
    {
        if (!strcmp (argv[i], "-present_bufs") && (argc - i > 1))
        {
            sscanf (argv[i + 1], "%u", &present_bufs);
            break;
        }
    }
}
