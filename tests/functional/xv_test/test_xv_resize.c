#include <xcb/xcb.h>
#include <xcb/xv.h>
#include <xcb/shm.h>
#include <xcb/dri2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xf86drm.h>
#include <fcntl.h>
#include <unistd.h>
#include <tbm_bufmgr.h>
#include <assert.h>
#include "xv_test.h"
#include "data.h"
#include "xcb_api.h"

typedef struct
{
    xcb_atom_t atom_hflip;
    xcb_atom_t atom_vflip;
    xcb_atom_t atom_rotate;
    xcb_atom_t atom_buffer;
    error_s error;
} test_atom_s;

typedef struct
{
    size_t steps;
    rectangle_s *src_crop_p;
    rectangle_s *dst_crop_p;
} route_map_s;

typedef struct
{
    error_s error;
    route_map_s route_map;
} get_route_map_s;

typedef struct
{
    xcb_xv_port_t xv_open_port;
    test_atom_s atoms;
    xcb_connection_t *xcb_conn_p;
    xcb_screen_t *screen_p;
    xcb_window_t window_id;
    xcb_gcontext_t gc_id;
    route_map_s route_map;
} test_data_s;

static test_data_s *st_test_data_p = NULL;

static error_s test_xv_resize_prepare(xcb_connection_t *xcb_conn_p,
                                      xcb_screen_t * screen_p, rule_s rule);

static error_s test_xv_resize_run(xcb_connection_t *xcb_conn_p,
                                  xcb_screen_t * screen_p, rule_s rule);

static error_s test_xv_resize_close(xcb_connection_t *xcb_conn_p,
                                    xcb_screen_t * screen_p, rule_s rule);

test_atom_s
create_atoms (xcb_connection_t *xcb_conn_p)
{
    char const *atom_name_a[4] = {
        "_USER_WM_PORT_ATTRIBUTE_HFLIP",
        "_USER_WM_PORT_ATTRIBUTE_VFLIP",
        "_USER_WM_PORT_ATTRIBUTE_ROTATION",
        "XV_RETURN_BUFFER"};
    xcb_intern_atom_cookie_t atom_ck_a[4] = {};
    xcb_intern_atom_reply_t *atom_reply_p = NULL;
    test_atom_s ret = {};
    xcb_generic_error_t *error_p = NULL;
    int i;
    for (i = 0; i < 4; i++)
    {
        atom_ck_a[i] =
                xcb_intern_atom (xcb_conn_p, 0, (uint16_t) strlen(atom_name_a[i]),
                                 atom_name_a[i]);
    }
    for (i = 0; i < 4; i++)
    {
        atom_reply_p =
                xcb_intern_atom_reply (xcb_conn_p , atom_ck_a[i], &error_p);

        if (error_p)
        {
            ret.error.error_str = "Can't register atom";
            ret.error.error_code = error_p->error_code;
            goto close_l;
        }
        if (atom_reply_p == NULL)
        {
            ret.error.error_str = "Can't register atom";
            goto close_l;
        }
        switch (i)
        {
        case 0:
            ret.atom_hflip = atom_reply_p->atom;
            break;
        case 1:
            ret.atom_vflip = atom_reply_p->atom;
            break;
        case 2:
            ret.atom_rotate = atom_reply_p->atom;
            break;
        case 3:
            ret.atom_buffer = atom_reply_p->atom;
            break;
        }
        free(atom_reply_p);
        atom_reply_p = NULL;
    }

close_l:

    if (error_p)
        free(error_p);
    if (atom_reply_p)
        free(atom_reply_p);
    return ret;
}

get_route_map_s
create_route_map (rule_s *rules)
{
    get_route_map_s ret = {};
    rectangle_s *src_crop_p = NULL;
    rectangle_s *dst_crop_p = NULL;
    if (rules == NULL)
    {
        ret.error.error_str = "Wrong input arguments";
        goto close_l;
    }
    size_t steps_width = (rules->max_width - rules->min_width)/10;
    size_t steps_height = (rules->max_height - rules->max_height)/10;
    size_t steps = (max(steps_width, steps_height))*2;

    src_crop_p = calloc(steps, sizeof(rectangle_s));
    if (!src_crop_p)
    {
        ret.error.error_str = "Can't alloc memory";
        goto close_l;
    }
    dst_crop_p = calloc(steps, sizeof(rectangle_s));
    if (!dst_crop_p)
    {
        ret.error.error_str = "Can't alloc memory";
        goto close_l;
    }
//    pace_dst_crop.height = (uint32_t)
//            ((double)(rules->max_size.height - rules->min_size.height)/
//             (rules->steps * 2));
//    pace_dst_crop.width = (uint32_t)
//            ((double)(rules->max_size.width - rules->min_size.width)/
//             (rules->steps * 2));
//    pace_src_crop.height = (uint32_t)
//            ((double)(rules->image_height - rules->min_size.height)/
//             (rules->steps * 2));
//    pace_src_crop.width = (uint32_t)
//            ((double)(rules->image_width - rules->min_size.width)/
//             (rules->steps * 2));
    if (!rules->src_change)
    {
        src_crop_p[0].x = 0;
        src_crop_p[0].y = 0;
        src_crop_p[0].width = rules->image_width;
        src_crop_p[0].height = rules->image_height;
    }
    else
    {
        src_crop_p[0].x = 0;
        src_crop_p[0].y = 0;
        src_crop_p[0].width = rules->min_width;
        src_crop_p[0].height = rules->min_height;
    }

    if (!rules->dst_change)
    {
        dst_crop_p[0].x = 0;
        dst_crop_p[0].y = 0;
        dst_crop_p[0].width = rules->image_width;
        dst_crop_p[0].height = rules->image_height;
    }
    else
    {
        dst_crop_p[0].x = 0;
        dst_crop_p[0].y = 0;
        dst_crop_p[0].width = rules->min_width;
        dst_crop_p[0].height = rules->min_height;
    }
    int i;
    for (i = 1; i < (steps/2); i++)
    {
        if (!rules->src_change)
        {
            src_crop_p[i].x = 0;
            src_crop_p[i].y = 0;
            src_crop_p[i].width = rules->image_width;
            src_crop_p[i].height = rules->image_height;
        }
        else
        {
            src_crop_p[i].x = 0;
            src_crop_p[i].y = 0;
            src_crop_p[i].width =
                    ((src_crop_p[i-1].width + 10) <= rules->image_width) ?
                        (src_crop_p[i-1].width + 10) : src_crop_p[i-1].width;
            src_crop_p[i].height =
                    ((src_crop_p[i-1].height + 10) <= rules->image_height) ?
                        (src_crop_p[i-1].height + 10) : src_crop_p[i-1].height;
        }
        if (!rules->dst_change)
        {
            dst_crop_p[i].x = 0;
            dst_crop_p[i].y = 0;
            dst_crop_p[i].width = rules->image_width;
            dst_crop_p[i].height = rules->image_height;
        }
        else
        {
            dst_crop_p[i].x = 0;
            dst_crop_p[i].y = 0;
            dst_crop_p[i].width =
                    ((dst_crop_p[i-1].width + 10) <= rules->max_width) ?
                    (dst_crop_p[i-1].width + 10) : dst_crop_p[i-1].width;
            dst_crop_p[i].height =
                    ((dst_crop_p[i-1].height + 10) <= rules->max_height) ?
                    (dst_crop_p[i-1].height + 10) : dst_crop_p[i-1].height;
        }
    }
    for (i = (steps/2); i < steps; i++)
    {
        if (!rules->src_change)
        {
            src_crop_p[i].x = 0;
            src_crop_p[i].y = 0;
            src_crop_p[i].width = rules->image_width;
            src_crop_p[i].height = rules->image_height;
        }
        else
        {
            src_crop_p[i].x = 0;
            src_crop_p[i].y = 0;
            src_crop_p[i].width =
                    ((src_crop_p[i-1].width - 10) >= rules->min_width) ?
                        (src_crop_p[i-1].width - 10) : src_crop_p[i-1].width;
            src_crop_p[i].height =
                    ((src_crop_p[i-1].height - 10) >= rules->min_height) ?
                        (src_crop_p[i-1].height - 10) : src_crop_p[i-1].height;
        }
        if (!rules->dst_change)
        {
            dst_crop_p[i].x = 0;
            dst_crop_p[i].y = 0;
            dst_crop_p[i].width = rules->image_width;
            dst_crop_p[i].height = rules->image_height;
        }
        else
        {
            dst_crop_p[i].x = 0;
            dst_crop_p[i].y = 0;
            dst_crop_p[i].width =
                    ((dst_crop_p[i-1].width - 10) >= rules->min_width) ?
                    (dst_crop_p[i-1].width - 10) : dst_crop_p[i-1].width;
            dst_crop_p[i].height =
                    ((dst_crop_p[i-1].height - 10) >= rules->min_height) ?
                    (dst_crop_p[i-1].height - 10) : dst_crop_p[i-1].height;
        }
    }

    ret.route_map.src_crop_p = src_crop_p;
    ret.route_map.dst_crop_p = dst_crop_p;
    ret.route_map.steps = steps;
    return ret;
close_l:
    if (src_crop_p)
        free(src_crop_p);
    if (dst_crop_p)
        free(dst_crop_p);
    return ret;
}
test_case_s
test_xv_resize_init (void)
{
    test_case_s ret = {};
    ret.prepare_test = test_xv_resize_prepare;
    ret.run_test = test_xv_resize_run;
    ret.close_test = test_xv_resize_close;
    return ret;
}


static error_s
test_xv_resize_prepare(xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p, rule_s rule)
{
    error_s ret = {};
    st_test_data_p = malloc(sizeof(test_data_s));
    DBG;
    if (!st_test_data_p)
    {
        ret.error_str = "Can't alloc memory for test_data_s";
        ret.error_code = 0;
        goto close_l;
    }
    st_test_data_p->xcb_conn_p = xcb_conn_p;
    st_test_data_p->screen_p = screen_p;
    DBG;
    get_xv_port_s xv_port = open_adaptor (xcb_conn_p, screen_p->root);
    if (xv_port.error.error_str)
    {
        ret = xv_port.error;
        goto close_l;
    }
    DBG;
    st_test_data_p->xv_open_port = xv_port.xv_port_id;
    st_test_data_p->atoms = create_atoms (xcb_conn_p);
    if(st_test_data_p->atoms.error.error_str)
    {
        ret = st_test_data_p->atoms.error;
        goto close_l;
    }
    DBG;

    get_window_s window = create_window(xcb_conn_p, screen_p);
    if (window.error.error_str)
    {
        ret = window.error;
        goto close_l;
    }
    st_test_data_p->window_id = window.window_id;
    get_gc_s gc = create_gc (xcb_conn_p, st_test_data_p->window_id, screen_p);
    if (gc.error.error_str)
    {
        ret = gc.error;
        goto close_l;
    }
    st_test_data_p->gc_id = gc.gc_id;

    get_route_map_s route = create_route_map (&rule);

    if (route.error.error_str)
    {
        ret = route.error;
        goto close_l;
    }

    st_test_data_p->route_map = route.route_map;
    return ret;

close_l:
    DBG;
    if (st_test_data_p)
        free(st_test_data_p);
/* TODO: close port */
    st_test_data_p = NULL;
    return ret;
}
static
error_s
test_xv_resize_run(xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p, rule_s rule)
{

    DBG;
    xcb_generic_error_t  *error_p = NULL;
    error_s ret = {};
    shm_data_s shm;
    if (st_test_data_p == NULL)
    {
        ret.error_str = "Wrong argument";
        return ret;
    }
    frame_attr_s frame[st_test_data_p->route_map.steps];

    DBG;
    get_image_attr_s image_attr =
            get_image_attr (xcb_conn_p, st_test_data_p->xv_open_port,
                            rule.image_fourcc, rule.image_width, rule.image_height);
    DBG;
    if (image_attr.error.error_str)
    {
        ret = image_attr.error;
        goto close_l;
    }
    /* get raw data */

    size_t max_j = rule.count_uniqes_frames;
    DBG;
    int i,j = 0;
    for (i = 0; i < st_test_data_p->route_map.steps; i++)
    {

        if (i == 0)
            printf("Generate data:\n");
        if (i < max_j)
        {
            shm = get_image_random (xcb_conn_p, screen_p,
                                    image_attr.image.sizes,
                                    image_attr.image.num_planes);
            if (shm.error.error_str)
            {
                ret = shm.error;
                goto close_l;
            }
            frame[i].segment = shm.shm_seg_id;
            printf("%f%%\n", ((double)i/max_j));
        }
        else
        {
            if (j == max_j)
            {
                j = 0;
            }
            frame[i].segment = frame[j].segment;
            j++;
        }

        frame[i].image_p = &image_attr.image;
        frame[i].src_crop_p = &st_test_data_p->route_map.src_crop_p[i];
        frame[i].dst_crop_p = &st_test_data_p->route_map.dst_crop_p[i];

    }
    DBG;
    xcb_void_cookie_t map_window_ck =
            xcb_map_window_checked (xcb_conn_p, st_test_data_p->window_id);
    xcb_void_cookie_t put_image_ck =
    put_image (xcb_conn_p, st_test_data_p->xv_open_port,
               st_test_data_p->window_id, st_test_data_p->gc_id, &frame[0]);
    j = 1;

    error_p = xcb_request_check (xcb_conn_p, map_window_ck);
    if (error_p)
    {
        ret.error_str = "Can't make transparent window";
        ret.error_code = error_p->error_code;
        goto close_l;
    }

    xcb_generic_event_t  *e;
    xcb_client_message_event_t* msg;
    DBG;
    while (1)
    {
        e = xcb_poll_for_event(xcb_conn_p);
        while (e)
        {
            switch (e->response_type)
            {
            case XCB_CLIENT_MESSAGE:
                msg = (xcb_client_message_event_t *)e;
                if (msg->type == st_test_data_p->atoms.atom_buffer)
                {
                    unsigned int keys[3] = {0, };
                    keys[0] = msg->data.data32[0];
                    keys[1] = msg->data.data32[1];
                    keys[2] = msg->data.data32[2];
                    printf ("receive: %d, %d, %d (<= Xorg)\n",
                            keys[0], keys[1], keys[2]);
                }

                break;
            case XCB_EXPOSE: 
                break;
            default:
                break;
            }
            free(e);
            e = xcb_poll_for_event(xcb_conn_p);
        }

        error_p = xcb_request_check (xcb_conn_p, put_image_ck);
        if (error_p)
        {
            ret.error_str = "Can't draw image frame";
            ret.error_code = error_p->error_code;
            goto close_l;
        }
        if (j >= st_test_data_p->route_map.steps)
            j = 0;
        put_image_ck = put_image (xcb_conn_p, st_test_data_p->xv_open_port,
                                  st_test_data_p->window_id, st_test_data_p->gc_id,
                                  &frame[j]);
        printf("src:(%dwx%dh) dst: (%dwx%dh)\n",
               frame[j].src_crop_p->width, frame[j].src_crop_p->height,
               frame[j].dst_crop_p->width, frame[j].dst_crop_p->height);
        j++;
       usleep(1000000/rule.frame_per_second);
    }

close_l:
    if (error_p)
        free(error_p);
    return ret;
}

static error_s
test_xv_resize_close(xcb_connection_t *xcb_conn_p, xcb_screen_t * screen_p, rule_s rule)
{
    error_s ret = {};
    return ret;
}

#if 0
xcb_xv_list_image_formats_reply_t *list =
    xcb_xv_list_image_formats_reply (conn,
        xcb_xv_list_image_formats (conn, a->base_id), NULL);
if (list == NULL)
    return NULL;

/* Check available XVideo chromas */
xcb_xv_query_image_attributes_reply_t *attr = NULL;
unsigned rank = UINT_MAX;

for (const xcb_xv_image_format_info_t *f =
         xcb_xv_list_image_formats_format (list),
                                      *f_end =
         f + xcb_xv_list_image_formats_format_length (list);
     f < f_end;
     f++)
#endif
