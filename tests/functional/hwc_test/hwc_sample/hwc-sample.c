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

/* This file contains implementation of software/hardware composition manager.
   This app uses hwc, composite and damage extensions to create image of screen
   from window's contents by using copy_area/drm planes.*/

#include <stdio.h>
#include <stdlib.h>

#include <dlog.h>

#include <xcb/composite.h>
#include <xcb/damage.h>
#include <xcb/hwc.h>

#include "list.h"

#define _DEBUG_
#ifdef _DEBUG_
    #define DEBUG_OUT(format, args...) { ALOG(LOG_INFO, "HWC_SAMPLE", format, ##args); }
#else
    #define DEBUG_OUT(...) {;}
#endif

typedef struct cmd_options_t
{
    int not_use_hw_layers; // whether we want to use hardware layers to make compositing or not
} cmd_options_t;

// thus structure describes window in window's list
typedef struct window_t
{
    struct list_head list_member;
    xcb_window_t wnd_id;            // X11 client sees only this id
    xcb_window_t parent_id;         // parent, after reparenting !
    xcb_pixmap_t wnd_content;       // refered to off-screen storage of redirected window
    xcb_hwc_draw_info_t hwc_info;   // info for use in hwc_set_drawable
    uint8_t use_hw_layer;           // whether this window must be compositing via hardware layer
    uint8_t off_hw_layer;           // user can specify use or not hw layer for this window
    uint8_t is_hidden;              // is window hidden, this means window doesn't participate
                                    // in composition
    xcb_damage_damage_t damage;
} window_t;

typedef struct window_manager_t
{
   struct list_head wnds_list_head;  // list of grabbed windows, arranged by stack order position
   u_int32_t amount;    // amout of windows in above list
   const xcb_query_extension_reply_t* damage_ext;   // for determine damage's events
   u_int32_t max_amount_hw_layers;   // maximum available hardware layers
   u_int32_t cur_amount_hw_layers;   // current amount of utilized hardware layers
   xcb_gcontext_t gc_soft_composite; // used for software compositing
   xcb_gcontext_t gc_fill_root_buf;  // used to fill root buffer

   // to avoid flickering, we just composite all windows to this pixmap, then, after
   // composition of all window completed, composite this pixmap to root window
   xcb_pixmap_t root_buffer;

   // information about windows which will be composited via hardware layers
   xcb_hwc_draw_info_t* hwc_info;
   cmd_options_t cmd_opts;          // cmd line options

   // window pushed out from hw layer stack, when some window was set on top of this stack,
   // this variable used for composite pushed out window in software only mode in time
   // when hw composition for this (some) window occur
   struct window_t* pushed_from_hw_stack;

} window_manager_t;

xcb_connection_t* xcb_dpy;
xcb_screen_t* xcb_screen;

window_manager_t wm;

// update root window
//========================================================================
static void
_update_root (void)
{
    // composite root_buffer to root window to show on screen
    xcb_copy_area (xcb_dpy, wm.root_buffer, xcb_screen->root, wm.gc_soft_composite, 0, 0,
                   0, 0, xcb_screen->width_in_pixels, xcb_screen->height_in_pixels);
    DEBUG_OUT (" composite root_buffer to root wnd.\n");
}

// update root_buffer, that can be then copied to root window via _update_root()
//========================================================================
static void
_update_root_buf (window_t* wnd)
{
    if (!wnd) return;

    // composite window to root_buffer
    xcb_copy_area (xcb_dpy, wnd->wnd_content, wm.root_buffer, wm.gc_soft_composite, 0, 0,
                   wnd->hwc_info.dstX, wnd->hwc_info.dstY, wnd->hwc_info.dstWidth,
                   wnd->hwc_info.dstHeight);
    DEBUG_OUT (" off-screen storage: 0x%06x of wnd: 0x%06x (child wnd: 0x%06x) has been"
               " composited to root_buffer at %hd, %hd, [%hux%hu].\n", wnd->wnd_content,
               wnd->parent_id, wnd->wnd_id, wnd->hwc_info.dstX, wnd->hwc_info.dstY,
               wnd->hwc_info.dstWidth, wnd->hwc_info.dstHeight);
}

// fill root buffer before composite on it
//========================================================================
static void
_fill_root_buffer (void)
{
    xcb_rectangle_t rect;

    rect.x = 0;
    rect.y = 0;
    rect.width = xcb_screen->width_in_pixels;
    rect.height = xcb_screen->height_in_pixels;

    xcb_poly_fill_rectangle (xcb_dpy, wm.root_buffer, wm.gc_fill_root_buf, 1, &rect);

    DEBUG_OUT ("root_buffer has been filled.\n");
}

//
//========================================================================
static void
_hwc_set_print_info (void)
{
    int i;

    DEBUG_OUT (" hwc_set_drawable:\n");

    for (i = 0; i < wm.cur_amount_hw_layers; i++)
        DEBUG_OUT ("  drawable: 0x%06x, composite_method: %d, from %hd, %hd [ %hux%hu], "
                   "to %hd, %hd [%hux%hu].\n",
                   wm.hwc_info[i].drawable, wm.hwc_info[i].composite_methods,
                   wm.hwc_info[i].srcX, wm.hwc_info[i].srcY, wm.hwc_info[i].srcWidth, wm.hwc_info[i].srcHeight,
                   wm.hwc_info[i].dstX, wm.hwc_info[i].dstY, wm.hwc_info[i].dstWidth, wm.hwc_info[i].dstHeight);
}

// do compositing of one drawable
// drawable - window id user application knows about which (passed root wnd will be ignored) !!!
//========================================================================
static void
composite_drawable (xcb_drawable_t drawable)
{
    window_t* wnd;
    int update_root = 0;

    // iterate over grabbed windows list, from topmost window to lowermost window
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        // look for window which must be recomposited
        if (wnd->wnd_id != drawable || wnd->wnd_id == xcb_screen->root) continue;

        // if we in software only composite mode
        if (wm.cmd_opts.not_use_hw_layers)
        {
            _update_root_buf (wnd);
            _update_root ();

            break;
        }
        else
        {
            // if wnd isn't on hw layer, copy window content to root window
            if (!wnd->use_hw_layer)
            {
                update_root = 1;
                _update_root_buf (wnd);
            }

            // if window is on hw layer it can pushed out, from hw layer stack, window that
            // was on hw layer previously (it can be occurred after MapRequest for some window),
            // so we must do sw composition for this pushed out window
            else if (wm.pushed_from_hw_stack)
            {
                update_root = 1;
                _update_root_buf (wm.pushed_from_hw_stack);
                wm.pushed_from_hw_stack = NULL;
            }

            // composite root_buffer to root window to show on screen, if root buffer was changed
            if (update_root)  _update_root ();

            // we must reset drawables, the main reason to do this, for windows which are on hw
            // layers, is that present extension in X server makes swap of buffers, not copy
            // (see present/present.c in Xorg) and we cann't distinguish no-dri, dri2 and present windows
            xcb_hwc_set_drawable (xcb_dpy, 0, wm.cur_amount_hw_layers, wm.hwc_info);           
            _hwc_set_print_info ();

            break;
        }
    }

    xcb_flush (xcb_dpy);
}

// do compositing of all windows, in any cases root window will be updated
//========================================================================
static void
composite_all (void)
{
    window_t* wnd;

    DEBUG_OUT ("compositing of all windows:\n");

    // fill root buffer before composite on it
    _fill_root_buffer ();

    // if user doesn't want to do hardware compositing
    if (wm.cmd_opts.not_use_hw_layers)
    {            
        // iterate over grabbed windows list, from lowermost window to topmost window
        // NOTE: this implementation of list ala stack, so we must use _prev suffix
        list_for_each_prev_entry (wnd, &wm.wnds_list_head, list_member)
        {
            if (wnd->wnd_id == xcb_screen->root)
                continue;

            _update_root_buf (wnd);
        }

        _update_root ();
    }
    else
    {
        list_for_each_prev_entry (wnd, &wm.wnds_list_head, list_member)
        {
            if (wnd->use_hw_layer || wnd->wnd_id == xcb_screen->root) continue;

            _update_root_buf (wnd);
        }

        // composite root_buffer to root window to show on screen,
        _update_root ();

        xcb_hwc_set_drawable (xcb_dpy, 0, wm.cur_amount_hw_layers, wm.hwc_info);
        _hwc_set_print_info ();
    }

    xcb_flush (xcb_dpy);
}

// return geometry of specified, by win_id, window (via second parameter)
// return -1 if fail
//========================================================================
static int
get_wnd_geometry (xcb_window_t win_id, xcb_get_geometry_reply_t* geo)
{
    xcb_get_geometry_cookie_t gg_c;
    xcb_get_geometry_reply_t* gg_r = NULL;
    xcb_generic_error_t* xcb_error = NULL;

    if (!geo) return -1;

    gg_c = xcb_get_geometry (xcb_dpy, win_id);
    gg_r = xcb_get_geometry_reply (xcb_dpy, gg_c, &xcb_error);
    if (xcb_error)
    {
        printf ("error while xcb_get_geometry call.\n");
        free (xcb_error);
        return -1;
    }

    memcpy (geo, gg_r, sizeof(xcb_get_geometry_reply_t));

    free (gg_r);

    return 0;
}

// turn on external window events tracking
// in this function we ask X server to send us DestroyNotify, UnmapNotify events
// for window specified via @wnd->wnd_id
//========================================================================
static int
turn_on_track_ext_wnd_events (window_t* wnd)
{
    xcb_void_cookie_t cookie;
    xcb_generic_error_t* xcb_error;

    uint32_t value_mask;
    uint32_t value_list[1];

    if (!wnd) return -1;

    value_mask = XCB_CW_EVENT_MASK;
    value_list[0] = XCB_EVENT_MASK_STRUCTURE_NOTIFY;

    // ask X server to send us DestroyNotify, UnmapNotify and other notify events
    // for @wnd->wnd_id window
    cookie = xcb_change_window_attributes_checked (xcb_dpy, wnd->wnd_id, value_mask, value_list);
    if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_change_window_attributes call.\n");
        free (xcb_error);
        return -1;
    }

    printf (" turn on events tracking for external window: 0x%06x.\n\n", wnd->wnd_id);

    return 0;
}

// TODO:
//========================================================================
static void
unreparent_wnd (xcb_window_t wnd_id, xcb_window_t parent_id)
{
    xcb_destroy_window (xcb_dpy, parent_id);
}

// create new window @parent_id with parent like parent of @wnd_id,
// with same geometry, visual, border width and depth, and reparent
// @wnd_id to new parent @parent_id
//========================================================================
static int
reparent_wnd (xcb_window_t wnd_id, xcb_window_t* parent_id)
{
    xcb_get_geometry_reply_t geo;

    xcb_get_window_attributes_cookie_t get_attr_c;
    xcb_get_window_attributes_reply_t* get_attr_r;
    xcb_generic_error_t* xcb_errors = NULL;

    xcb_query_tree_cookie_t query_tree_c;
    xcb_query_tree_reply_t* query_tree_r;

    xcb_void_cookie_t cookie;
    int res;

    if (!parent_id) return -1;

    get_attr_c = xcb_get_window_attributes (xcb_dpy, wnd_id);
    get_attr_r = xcb_get_window_attributes_reply (xcb_dpy, get_attr_c, &xcb_errors);
    if (xcb_errors)
    {
        printf ("error while xcb_get_window_attributes call.\n");
        free (xcb_errors);
        return -1;
    }

    // for determine parent of window, identifiable by wnd_id
    query_tree_c = xcb_query_tree (xcb_dpy, wnd_id);
    query_tree_r = xcb_query_tree_reply (xcb_dpy, query_tree_c, &xcb_errors);
    if (xcb_errors)
    {
        printf ("error while query_tree call.\n");
        free (xcb_errors);
        goto fail_1;
    }

    res = get_wnd_geometry (wnd_id, &geo);
    if (res < 0)
        goto fail_2;

    *parent_id = xcb_generate_id (xcb_dpy);

    cookie = xcb_create_window_checked (xcb_dpy, geo.depth, *parent_id, query_tree_r->parent,
                               geo.x, geo.y, geo.width, geo.height, geo.border_width,
                               get_attr_r->_class, get_attr_r->visual, 0, NULL);
    if ((xcb_errors = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_create_window call.\n");
        free (xcb_errors);
        goto fail_2;
    }

    cookie = xcb_reparent_window_checked (xcb_dpy, wnd_id, *parent_id, 0, 0);
    if ((xcb_errors = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_reparent_window call.\n");
        xcb_destroy_window (xcb_dpy, *parent_id);
        free (xcb_errors);
        goto fail_2;
    }

    xcb_map_window (xcb_dpy, wnd_id);

    free (query_tree_r);
    free (get_attr_r);

    return 0;

    // we cann't reparent window
fail_2:
    free (query_tree_r);
fail_1:
    free (get_attr_r);
    return -1;
}

// this function determines for which windows hw layers will be used, for composition
// Note: only this function can change @wnd->use_hw_layer field !!!
//========================================================================
static void
_reorder_hw_layers_stack (void)
{
    window_t* wnd;
    int i = 0;

    // iterate over grabbed windows list, from topmost window to lowermost window
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        // if window isn't hidden, if user allowed to use hw layer for this window
        if (wnd->wnd_id != xcb_screen->root && !wnd->is_hidden && !wnd->off_hw_layer && i < (wm.max_amount_hw_layers - 1))
        {
            i++;
            wnd->use_hw_layer = 1;
        }
        else
        {
            // in this branch, only one window can has use_hw_layer field set to 1
            // this is window pushed out from hw layers stack, so we must save it
            // and, then, when we will do composition for window that push, we also
            // will do composition for pushed out window
            if (wnd->use_hw_layer)
                wm.pushed_from_hw_stack = wnd;

            wnd->use_hw_layer = 0; // all other window will be composited via sw way, except root wnd
        }
    }

    // Note: if some window was mapped it can result to push out lowest window (in hw layers stack)
    //       from hw layers stack. And if window was unmapped (hidden) in can result to push in
    //       window (for which wasn't available hw layer previously) to hw layer stack.
}

// this function fills wm.hwc_info array used for hw composition (hwc_set_drawable request)
// we must call this function when:
//  we have grabbed window - new grabbed window must be set on topmost hw layer
//  wnd hierarchy is changed - to show this changes
//  wnd geometry has been changed - to show this changes
//  wnd was unmapped (hidden) - to show this changes
// NOTE: only this function can change wm.hwc_info array !!!
//========================================================================
static void
set_hw_layers_stack (void)
{
    window_t* wnd;
    int i = 0;

    _reorder_hw_layers_stack ();

    // reset hw layers stack
    memset (wm.hwc_info, 0, wm.max_amount_hw_layers * sizeof(xcb_hwc_draw_info_t));

    // iterate over grabbed windows list, from topmost window to lowermost window
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        // root is lowermost window in list
        if (wnd->wnd_id == xcb_screen->root)
            memcpy (&wm.hwc_info[i++], &wnd->hwc_info, sizeof(wnd->hwc_info));
        else if (wnd->use_hw_layer && i < (wm.max_amount_hw_layers - 1))
            memcpy (&wm.hwc_info[i++], &wnd->hwc_info, sizeof(wnd->hwc_info));
    }

    // set current amount of utilized hw layers to use in hwc_set_drawable request
    wm.cur_amount_hw_layers = i;
}

// init/reinit info for set window on hw layer
// wnd - id of window which hwc_info must be changed
// return -1 if fail
//========================================================================
static int
init_hwc_info (window_t* wnd)
{
    xcb_get_geometry_reply_t geo;
    int res;

    if (!wnd) return -1;

    res = get_wnd_geometry (wnd->wnd_id, &geo);
    if (res < 0) return -1;

    wnd->hwc_info.drawable = wnd->wnd_id;

    wnd->hwc_info.srcX = 0;
    wnd->hwc_info.srcY = 0;
    wnd->hwc_info.srcWidth = geo.width;
    wnd->hwc_info.srcHeight = geo.height;

    wnd->hwc_info.dstX = geo.x;
    wnd->hwc_info.dstY = geo.y;
    wnd->hwc_info.dstWidth = geo.width;
    wnd->hwc_info.dstHeight = geo.height;

    wnd->hwc_info.composite_methods = XCB_HWC_COMPOSITE_METHOD_DEFAULT;

    return 0;
}

// ungrab window - forget about window
// undo operations were done when window was grabbed
//========================================================================
static void
ungrab_wnd (xcb_window_t wnd_id)
{
    window_t* wnd;
    int res = 0;

    // iterate over grabbed windows list
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        if (wnd->wnd_id == wnd_id)
        {
            res = 1;
            break;
        }
    }

    // check if we are asked to ungrab ungrabbed window
    if (!res) return;

    // if we here we must ungrab window

    printf ("ungrab window: 0x%06x\n", wnd_id);

    wm.amount--;

    // just undo what have done when window grabbing was
    xcb_damage_destroy (xcb_dpy, wnd->damage);
    xcb_free_pixmap (xcb_dpy, wnd->wnd_content);
    xcb_composite_unredirect_window (xcb_dpy, wnd->parent_id, XCB_COMPOSITE_REDIRECT_MANUAL);
    xcb_destroy_window (xcb_dpy, wnd->parent_id);
    list_del (&wnd->list_member);
    free (wnd);

    // we destroy (unlink) window, so we must reset hw layers stack to new condition
    if (!wm.cmd_opts.not_use_hw_layers)
        set_hw_layers_stack ();
}

// grab window - 'grab' means allocate window_t structure, add it to list of grabbed windows,
// fill it fields, reparent window, redirect parent window, obtain pixmap refered to off-screen
// storage of parent window, ask notice about damage on window, turn on external window events
// tracking and prepare hw layer stack
// wnd_id - id of window to grab
//========================================================================
static void
grab_wnd (xcb_window_t wnd_id)
{
    window_t* wnd;
    xcb_generic_error_t* xcb_error;
    xcb_void_cookie_t cookie;
    int res;

    // iterate over grabbed windows list, maybe we have grabbed window already
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        if (wnd->wnd_id == wnd_id) return;
    }

    printf ("grab window: 0x%06x\n", wnd_id);

    // if we here, window hasn't be grabbed yet, so grap it
    wnd = (window_t*)calloc (1, sizeof(window_t));

    // add window to begin of grabbed windows list
    // so we will obtain stack-ala list
    list_add (&wnd->list_member, &wm.wnds_list_head);

    wnd->wnd_id = wnd_id;

    // prepare information to use in hwc_set_drawable request
    res = init_hwc_info (wnd);
    if (res < 0)
        goto fail_1;

    // we mustn't reparent, redirect, create damage, obtain off-screen pixmap
    // and reoder hw layers stack for root window
    if (wnd->wnd_id != xcb_screen->root)
    {
        cookie = xcb_grab_server_checked (xcb_dpy);
        if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
        {
            printf ("error while xcb_grab_server call.\n");
            free(xcb_error);
            goto fail_1;
        }

        res = reparent_wnd (wnd->wnd_id, &wnd->parent_id);
        if (res < 0)
            goto fail_1;

        cookie = xcb_composite_redirect_window_checked (xcb_dpy, wnd->parent_id, XCB_COMPOSITE_REDIRECT_MANUAL);
        if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
        {
            printf ("error while xcb_composite_redirect_window call.\n");
            free(xcb_error);
            goto fail_2;
        }

        printf (" window: 0x%06x (child wnd: 0x%06x) has been redirected to off-screen storage.\n",
                wnd->parent_id, wnd->wnd_id);

        xcb_map_window (xcb_dpy, wnd->parent_id);

        // obtain pixmap refered to off-screen storage of redirected window
        wnd->wnd_content = xcb_generate_id (xcb_dpy);
        cookie = xcb_composite_name_window_pixmap_checked (xcb_dpy, wnd->parent_id, wnd->wnd_content);
        if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
        {
            printf ("error while xcb_composite_name_window_pixmap call.\n");
            free(xcb_error);
            goto fail_3;
        }

        printf (" off-screen storage: 0x%06x for window: 0x%06x (child wnd: 0x%06x) has been obtained.\n",
                wnd->wnd_content, wnd->parent_id, wnd->wnd_id);   

        // we track damage notification for window user know about which, not for it parent !
        wnd->damage = xcb_generate_id (xcb_dpy);
        cookie = xcb_damage_create_checked (xcb_dpy, wnd->damage, wnd->wnd_id, XCB_DAMAGE_REPORT_LEVEL_RAW_RECTANGLES);
        if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
        {
            printf ("error while xcb_damage_create call.\n");
            free(xcb_error);
            goto fail_4;
        }

        printf (" damage object: 0x%06x for window: 0x%06x (child wnd: 0x%06x) has been created.\n",
                wnd->damage, wnd->parent_id, wnd->wnd_id);

        cookie = xcb_ungrab_server_checked (xcb_dpy);
        if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
        {
            printf ("error while xcb_ungrab_server call.\n");
            free(xcb_error);
            goto fail_5;
        }

        // we must do this due to inability to set XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT and
        // XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY flags together (for root wnd), so we must track
        // events for all windows, via turn on tracking events for each window itself, no for
        // their parent, as we did for MAP_REQUEST event
        res = turn_on_track_ext_wnd_events (wnd);
        if (res < 0) goto fail_5;
    }

    // we have grabbed new window, so we must reset hw layers stack to new condition
    if (!wm.cmd_opts.not_use_hw_layers)
        set_hw_layers_stack ();

    wm.amount++;

    return;

    // we cann't grab this window
fail_5:
    xcb_damage_destroy (xcb_dpy, wnd->damage);
fail_4:
    xcb_free_pixmap (xcb_dpy, wnd->wnd_content);
fail_3:
    xcb_composite_unredirect_window (xcb_dpy, wnd->parent_id, XCB_COMPOSITE_REDIRECT_MANUAL);
fail_2:
    unreparent_wnd (wnd->wnd_id, wnd->parent_id);
fail_1:
    list_del (&wnd->list_member);
    free (wnd);

    return;
}

//
//========================================================================
/*static void
show_wnd (xcb_window_t wnd_id)
{

}*/

// when X11 client issue UnmapRequest for @wnd_id window we must remove this window
// from composition process, it isn't ungrabbing, we only 'hide' window from
// compositor and accordingly from user
//========================================================================
static void
hide_wnd (xcb_window_t wnd_id)
{
    window_t* wnd;
    xcb_generic_error_t* xcb_error;
    xcb_void_cookie_t cookie;
    int res = 0;

    // iterate over grabbed windows list
    list_for_each_entry (wnd, &wm.wnds_list_head, list_member)
    {
        if (wnd->wnd_id == wnd_id)
        {
            res = 1;
            break;
        }
    }

    // check if we are asked to hide ungrabbed window
    if (!res) return;

    // if we here we must hide window

    printf ("hide window: 0x%06x\n", wnd_id);

    // hide window to avoid it to be composition participant
    wnd->is_hidden = 1;

    // destroy damage to avoid unneccessary damage events, for this hidden window, handling
    cookie = xcb_damage_destroy_checked (xcb_dpy, wnd->damage);
    if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_damage_destroy call.\n");
        free(xcb_error);
    }

    // we hide window, so we must reset hw layers stack to new condition
    if (!wm.cmd_opts.not_use_hw_layers)
        set_hw_layers_stack ();
}

// check does @wnd_id window exist
// return -1 if doesn't, 0 otherwise
//========================================================================
static int
check_wnd_existing (xcb_window_t wnd_id)
{
    xcb_generic_error_t* xcb_error;
    xcb_void_cookie_t cookie;

    cookie = xcb_unmap_window_checked (xcb_dpy, wnd_id);
    if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
    {
        free(xcb_error);
        return -1;
    }

    return 0;
}

// handle X11 protocol events for children of root window
//========================================================================
static void
handle_x11_events (xcb_generic_event_t* xcb_event)
{
   xcb_generic_error_t* xcb_error;
   xcb_void_cookie_t cookie;
   int ret;

   if (!xcb_event) return;

   switch (xcb_event->response_type)
   {
   #ifdef _DEBUG_
       case XCB_CLIENT_MESSAGE:
       {
           xcb_client_message_event_t* client_msg;
           client_msg = (xcb_client_message_event_t*)xcb_event;

           DEBUG_OUT ("client message for window: 0x%06x, type: %u, format: %hhu.\n",
                      client_msg->window, client_msg->type, client_msg->format);
       }
       break;
   #endif

       // when another X11 client call MapRequest we are notified about this, and only we
       // can issue real MapRequest request
       case XCB_MAP_REQUEST:
       {
           xcb_map_request_event_t* map_request;
           map_request = (xcb_map_request_event_t*)xcb_event;

           printf ("somebody try to map window: 0x%06x.\n", map_request->window);

           grab_wnd (map_request->window);

           // after new window was grabbed we must composite it, only it
           composite_drawable (map_request->window);
       }
       break;

       // when X11 client unmaps window, via xcb_unmap_window or just close sX11 connection,
       // for example, just kills app's process
       case XCB_UNMAP_NOTIFY:
       {
           break;
           xcb_unmap_notify_event_t* unmap_notify;
           unmap_notify = (xcb_unmap_notify_event_t*)xcb_event;

           printf ("unmap notify: window: 0x%06x, event: 0x%06x.\n", unmap_notify->window,
                   unmap_notify->event);

           hide_wnd (unmap_notify->window);

           // check does window still exist (UnmapNotify can be sent just before DestroyNotify,
           // so we must not call composite_all() for XCB_UNMAP_NOTIFY handler, we will call it
           // for XCB_DESTROY_NOTIFY handler)
           xcb_grab_server (xcb_dpy);

           ret = check_wnd_existing (unmap_notify->window);
           if (ret < 0)
           {
               printf (" window: 0x%06x still doesn't exist :-).\n", unmap_notify->window);
               xcb_ungrab_server (xcb_dpy);
               break;
           }

           // after window was hidden we must composite all, currently...
           composite_all ();

           // TODO: it is very expensive way...
           xcb_ungrab_server (xcb_dpy);
       }
       break;

       // when X11 client destroys window, via xcb_destroy_window or just close sX11 connection,
       // for example, just kills app's process
       case XCB_DESTROY_NOTIFY:
       {
           xcb_destroy_notify_event_t* destroy_notify;
           destroy_notify = (xcb_destroy_notify_event_t*)xcb_event;

           printf ("destroy notify: window: 0x%06x, event: 0x%06x.\n", destroy_notify->window,
                   destroy_notify->event);

           ungrab_wnd (destroy_notify->window);

           // after window was ungrabbed we must composite all, currently...
           composite_all ();
       }
       break;

       default:
       {
           // handle damage events
           // when client app has finished draw operations we receive damage notification
           if (xcb_event->response_type == wm.damage_ext->first_event + XCB_DAMAGE_NOTIFY)
           {
               xcb_damage_notify_event_t* damage_ev;
               damage_ev = (xcb_damage_notify_event_t*)xcb_event;

               DEBUG_OUT ("damage notification for wnd: 0x%06x, area: %hd %hd [%hux%hu], geo: %hd %hd [%hux%hu].\n", damage_ev->drawable,
                       damage_ev->area.x, damage_ev->area.y, damage_ev->area.width, damage_ev->area.height,
                       damage_ev->geometry.x, damage_ev->geometry.y, damage_ev->geometry.width, damage_ev->geometry.height);

               cookie = xcb_damage_subtract_checked (xcb_dpy, damage_ev->damage, XCB_NONE, XCB_NONE);
               if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
               {
                   printf ("error while xcb_damage_subtract call.\n");
                   free(xcb_error);
               }

               // we do composite only for window damage event has received for which
               composite_drawable (damage_ev->drawable);
           }
       }
   }
}

// find all windows, which are root window's childrens and print information about them
// return -1 if fail
//========================================================================
static int
find_all_window_ids (xcb_window_t** wnds_list, u_int32_t* wnds_amount)
{
    xcb_query_tree_cookie_t q_c;
    xcb_query_tree_reply_t* q_r;
    xcb_generic_error_t* xcb_errors = NULL;
    int i;

    if (!wnds_list || !wnds_amount) return -1;

    q_c = xcb_query_tree (xcb_dpy, xcb_screen->root);
    q_r = xcb_query_tree_reply (xcb_dpy, q_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while query_tree call.\n");
        return -1;
    }

    // don't free *wnds_list, it points on no dynamic allocated memory
    *wnds_list = xcb_query_tree_children (q_r);
    *wnds_amount = xcb_query_tree_children_length (q_r);

    // print info -----------------------------------

    printf (" RootWindow: 0x%06x.\n", xcb_screen->root);
    printf (" root_return: 0x%06x.\n", q_r->root);
    printf (" parent_return: 0x%06x.\n", q_r->parent);
    printf (" nchildren_return: %u.\n", *wnds_amount);

    free (q_r);

    printf ("\nnumber    window's id    window's name\n");
    printf ("     0       0x%06x        root\n", xcb_screen->root);

    for (i = 0; i < *wnds_amount; i++)
    {
        xcb_get_property_cookie_t gp_c;
        xcb_get_property_reply_t* gp_r;
        char* window_name;

        gp_c = xcb_get_property (xcb_dpy, 0, (*wnds_list)[i], XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 0, 0);
        gp_r = xcb_get_property_reply (xcb_dpy, gp_c, &xcb_errors);
        if (xcb_errors)
        {
            free (xcb_errors);
            window_name = NULL;
        }
        else
        {
            if(!xcb_get_property_value_length (gp_r))
                window_name = NULL;
            else
                window_name = xcb_get_property_value (gp_r);

            free (gp_r);
        }

        printf ("     %d ", i + 1);
        printf ("      0x%06x ", (*wnds_list)[i]);
        printf ("       %s\n", window_name);

        if (window_name)
            free (window_name);

    }
    printf ("\n");

    return 0;
}

// fill ai structure by using information about external windows
// ask X server to give us messages, which will be generated to external
// windows (for interception them)
//==================================================================
static int
grab_external_wnds (void)
{
    xcb_window_t* wnds_list = NULL;
    uint32_t wnds_amount = 0;
    int i, res;

    xcb_window_t* wnds_list_cp = NULL;

    printf ("\ngrab root window:\n ");

    // add root window to list of grabbed windows
    grab_wnd (xcb_screen->root);

    printf ("grab all external windows:\n");

    res = find_all_window_ids (&wnds_list, &wnds_amount);
    if (res < 0) return -1;

    // if no external windows are on this time we don't grab them :-)
    if (!wnds_amount) return 0;

    // we must do copy, because wnds_list points to array
    // allocated inside xcb, so it will be rewritten on next xcb_query_tree call
    // we mustn't free memory wnds_list points into, because it is not dynamic
    // allocated memory
    wnds_list_cp = (xcb_window_t*)calloc (wnds_amount, sizeof(xcb_window_t));
    memcpy (wnds_list_cp, wnds_list, wnds_amount * sizeof(xcb_window_t));

    for (i = 0; i < wnds_amount; i++)
        grab_wnd (wnds_list_cp[i]);

    free (wnds_list_cp);

    return 0;
}

// prepare HWC extension for use
// return -1 if fail, otherwise amount of available layers(planes)
//========================================================================
static int
hwc_ext_prepare (xcb_connection_t* c, xcb_screen_t* screen)
{
    xcb_hwc_query_version_cookie_t q_c;
    xcb_hwc_query_version_reply_t* q_r;
    xcb_generic_error_t* xcb_errors = NULL;

    xcb_hwc_open_cookie_t o_c;
    xcb_hwc_open_reply_t* o_r;

    xcb_void_cookie_t s_i_c;
    uint32_t max_layer;

    if (!c || !screen) return -1;

    // TODO: define XCB_HWC_MAJOR_VERSION is set to 1, but current version of hwc extension,
    // supported by X server is 2
    q_c = xcb_hwc_query_version (c, 2, XCB_HWC_MINOR_VERSION);
    q_r = xcb_hwc_query_version_reply (c, q_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while HWC query_version call.\n");
        return -1;
    }
    else if (q_r->major_version != 2 || q_r->minor_version != XCB_HWC_MINOR_VERSION)
    {
        printf ("HWC extension versions mismatch (between server: %u.%u and client: %u.%u).\n",
                q_r->major_version, q_r->minor_version, XCB_HWC_MAJOR_VERSION, XCB_HWC_MINOR_VERSION);
        free (q_r);
        return -1;
    }

    printf ("HWC extension: %d.%d.\n", q_r->major_version, q_r->minor_version);

    free (q_r);

    o_c = xcb_hwc_open (c, 0);
    o_r = xcb_hwc_open_reply (c, o_c, &xcb_errors);
    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while HWC open call.\n");
        return -1;
    }

    max_layer = o_r->maxlayer;
    free (o_r);

    printf (" hwc_open: maximum amount of hardware layers available to use: %u.\n", max_layer);

    // TODO: no define for mask for this call (third parameter),
    //       and I'm not sure about second parameter
    s_i_c = xcb_hwc_select_input_checked (c, screen->root, 1);

    if ((xcb_errors = xcb_request_check (c, s_i_c)))
    {
        printf ("error while xcb_hwc_select_input call.\n");
        free(xcb_errors);
        return -1;
    }

    return max_layer;
}

// prepare DAMAGE extension for use
// return NULL if fail
//==================================================================
static const xcb_query_extension_reply_t*
damage_ext_prepare (xcb_connection_t* c)
{
    xcb_damage_query_version_cookie_t q_c;
    xcb_damage_query_version_reply_t* q_r;
    xcb_generic_error_t* xcb_errors = NULL;
    const xcb_query_extension_reply_t* damage_ext;

    if (!c) return NULL;

    // for determine damage's events
    damage_ext = xcb_get_extension_data (c, &xcb_damage_id);
    if (!damage_ext)
    {
        printf ("error while xcb_get_extension_data call.\n");
        return NULL;
    }

    q_c = xcb_damage_query_version (c, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
    q_r = xcb_damage_query_version_reply (c, q_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while DAMAGE extension query_version call.\n");
        return NULL;
    }
    else if (q_r->major_version != XCB_DAMAGE_MAJOR_VERSION || q_r->minor_version != XCB_DAMAGE_MINOR_VERSION)
    {
        printf ("DAMAGE extension versions mismatch (between server: %u.%u and client: %u.%u).\n",
                q_r->major_version, q_r->minor_version, XCB_DAMAGE_MAJOR_VERSION, XCB_DAMAGE_MINOR_VERSION);
        free (q_r);
        return NULL;
    }

    printf ("DAMAGE extension: %d.%d.\n", q_r->major_version, q_r->minor_version);
    printf (" major_opcode: %hhu, first_event: %hhu, first_erorr: %hhu.\n",
            damage_ext->major_opcode, damage_ext->first_event, damage_ext->first_error);

    free (q_r);

    return damage_ext;
}

// prepare COMPOSITE extension for use
// return -1 if fail
//========================================================================
static int
composite_ext_prepare (xcb_connection_t* c)
{
    xcb_composite_query_version_cookie_t q_c;
    xcb_composite_query_version_reply_t* q_r;
    xcb_generic_error_t* xcb_errors = NULL;

    if (!c) return -1;

    q_c = xcb_composite_query_version (c, XCB_COMPOSITE_MAJOR_VERSION, XCB_COMPOSITE_MINOR_VERSION);
    q_r = xcb_composite_query_version_reply (c, q_c, &xcb_errors);

    if (xcb_errors)
    {
        free (xcb_errors);
        printf ("error while COMPOSITE query_version call.\n");
        return -1;
    }
    else if (q_r->major_version != XCB_COMPOSITE_MAJOR_VERSION || q_r->minor_version != XCB_COMPOSITE_MINOR_VERSION)
    {
        printf ("COMPOSITE extension versions mismatch (between server: %u.%u and client: %u.%u).\n",
                q_r->major_version, q_r->minor_version, XCB_COMPOSITE_MAJOR_VERSION, XCB_COMPOSITE_MINOR_VERSION);
        free (q_r);
        return -1;
    }

    printf ("COMPOSITE extension: %d.%d.\n", q_r->major_version, q_r->minor_version);

    free (q_r);

    return 0;
}

// create root_buffer to composite on it contents of windows (to avoid flickering)
// after all window contents will be on root_buffer composite it to root window to show
// also we ask X server to send us MapRequest, ConfigureRequest, CirculateRequest and
// ResizeRequest events for all root's children.
// (these request are sent when somebody try to map, reconfigure or resize root's children
//  window, and we can decide what to do whith this request.
//  for example: when somebody issues MapRequest via xcb_map_window, real map isn't occured, but
//  MapRequest event is sent us and we can properly react on it.)
// also fill root_buffer pixmap and root window
// return -1 if fail
//========================================================================
static int
prepare_root (void)
{
    xcb_void_cookie_t cookie;
    xcb_generic_error_t* xcb_error;

    uint32_t value_mask;
    uint32_t value_list[1];

    wm.root_buffer = xcb_generate_id (xcb_dpy);
    cookie = xcb_create_pixmap_checked (xcb_dpy, xcb_screen->root_depth, wm.root_buffer, xcb_screen->root,
                                        xcb_screen->width_in_pixels, xcb_screen->height_in_pixels);
    if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_create_pixmap call.\n");
        free(xcb_error);
        return -1;
    }

    value_mask = XCB_CW_EVENT_MASK;
    value_list[0] = XCB_EVENT_MASK_RESIZE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT;

    // this call must be done after any windows create call !!!
    // also we ask X server to send us MapRequest, ConfigureRequest, CirculateRequest and
    // ResizeRequest events for all root's children.
    cookie = xcb_change_window_attributes_checked (xcb_dpy, xcb_screen->root, value_mask, value_list);
    if ((xcb_error = xcb_request_check (xcb_dpy, cookie)))
    {
        printf ("error while xcb_change_window_attributes call.\n");
        free(xcb_error);
        return -1;
    }

    return 0;
}

//
//========================================================================
static xcb_gcontext_t
create_gc (xcb_connection_t* c, xcb_screen_t* screen, uint32_t foreground)
{
    xcb_gcontext_t gc;
    xcb_void_cookie_t cookie;
    xcb_generic_error_t* error;
    uint32_t value[2];

    if (!c || !screen)
    {
        printf ("error while create_gc call.\n");
        exit (EXIT_FAILURE);
    }

    value[0] = XCB_GX_COPY;
    value[1] = foreground;

    gc = xcb_generate_id (c);
    cookie = xcb_create_gc_checked (c, gc, screen->root, XCB_GC_FUNCTION | XCB_GC_FOREGROUND, value);
    xcb_flush (c);

    if ((error = xcb_request_check (c, cookie)))
    {
        printf ("error while xcb_create_gc call.\n");
        free(error);
        exit (EXIT_FAILURE);
    }

    return gc;
}

//
//==================================================================
static void
usage (char *argv)
{
    printf ("This application is simple sw/hw compositor manager. It can be used to test "
            "ddx's side of hwc, dri2 and dri3/present extensions.\n"
            "You can launch clients apps and then launch this compositor manager, or you "
            "can launch firstly compositor manager and then\nclient apps.\n\n");

    printf ("%s usage : \n", argv);
    printf ("   %s (options) (parameters)\n", argv);
    printf ("  (options) :\n");
    printf ("   -no_hw   : don't use hw layers to composition (only software composition)\n");
}

// parse command line
// returns -1 if failed
//==================================================================
static int
check_options (int argc, char *argv[])
{
    int j = 0;

    if (!argv) return -1;

    // check if we want to know how use this app
    for (j = 1; j < argc; j++)
    {
        if (!strcmp (argv[j], "--help"))
        {
             usage (argv[0]);
             return -1;
        }
    }

    // check if we don't want to use hardware layers
    for (j = 1; j < argc; j++)
    {
        if (!strcmp (argv[j], "--no_hw"))
        {
             wm.cmd_opts.not_use_hw_layers = 1; // we will do only software composition
             break;
        }
    }

    return 0;
}

//
//========================================================================
int
main (int argc, char *argv[])
{
    xcb_generic_event_t* xcb_event = NULL;
    int res;

    // parse cmdline
    res = check_options (argc, argv);
    if (res < 0) goto fail;

    // open the connection to the X server
    xcb_dpy = xcb_connect (NULL, NULL);
    res = xcb_connection_has_error (xcb_dpy);
    if (res > 0)
    {
        printf ("error while open X display.\n");
        goto fail;
    }

    // get first screen of display
    xcb_screen = xcb_setup_roots_iterator (xcb_get_setup (xcb_dpy)).data;
    if (!xcb_screen)
    {
        printf ("error while obtain first screen of X display.\n");
        goto fail;
    }

    wm.gc_soft_composite = create_gc (xcb_dpy, xcb_screen, 0x00);
    wm.gc_fill_root_buf  = create_gc (xcb_dpy, xcb_screen, 0x555555);

    res = prepare_root ();
    if (res < 0) goto fail;

    // prepare extensions needed for this window manager
    res = composite_ext_prepare (xcb_dpy);
    if (res < 0) goto fail;

    wm.damage_ext = damage_ext_prepare (xcb_dpy);
    if (!wm.damage_ext) goto fail;

    res = hwc_ext_prepare (xcb_dpy, xcb_screen);
    if (res < 0) goto fail;

    wm.max_amount_hw_layers = res;

    // if we haven't available hardware layers, we will use only software compositing
    if (wm.max_amount_hw_layers)
        wm.hwc_info = calloc (wm.max_amount_hw_layers, sizeof(xcb_hwc_draw_info_t));

    // init list of grabbed windows
    list_init (&wm.wnds_list_head);

    // grab external windows
    res = grab_external_wnds ();
    if (res < 0) goto fail;

    // in this point we have list of grabbed windows, arranged by stack order position,
    // so we can do compositing (software or hardware depend on not_use_hw_layers flag and amount of
    // available hardware layers)

    composite_all ();

    // events handle loop

    while (1)
    {
        xcb_flush (xcb_dpy);
        xcb_event = xcb_wait_for_event (xcb_dpy);
        if (!xcb_event)
        {
            printf ("event returned by xcb_wait_for_event is NULL.\n");
            goto fail;
        }

        DEBUG_OUT ("event: %hhu.\n", xcb_event->response_type);

        handle_x11_events (xcb_event);
        free (xcb_event);
    }

fail:
    printf ("abnormal exit.\n");

    if (wm.gc_fill_root_buf)
        xcb_free_gc (xcb_dpy, wm.gc_fill_root_buf);

    if (wm.gc_soft_composite)
        xcb_free_gc (xcb_dpy, wm.gc_soft_composite);

    if (xcb_dpy)
        xcb_disconnect (xcb_dpy);

    return 1;
}
