#include "xv_test.h"
#include <xcb/xcb.h>
#include <xcb/xv.h>
#include <xcb/shm.h>
#include <xcb/dri2.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <xcb/dri2.h>
#include <xf86drm.h>
#include <fcntl.h>
#include "xv_types.h"
#include <tbm_bufmgr.h>
#include "data.h"
#include <sys/time.h>
#include "test_xv_resize.h"

typedef struct
{
    size_t num_tests;
    test_case_s * test_case_array;
    error_s * error_array;
} test_map_s;

error_s
check_xv (xcb_connection_t *xcb_conn_p)
{
    xcb_xv_query_extension_cookie_t xv_cookie;
    xcb_xv_query_extension_reply_t *xv_reply = NULL;
    xcb_generic_error_t * error_p = NULL;
    error_s ret = {};
    xv_cookie = xcb_xv_query_extension (xcb_conn_p);
    xv_reply = xcb_xv_query_extension_reply (xcb_conn_p, xv_cookie, &error_p);
    if (error_p)
    {
        ret.error_str = "Can't find xv extension";
        ret.error_code = error_p->error_code;
    }

    if (xv_reply)
        free(xv_reply);
    if (error_p)
        free(error_p);
    return ret;
}

error_s
check_shm (xcb_connection_t *xcb_conn_p)
{
    xcb_shm_query_version_cookie_t shm_cookie;
    xcb_shm_query_version_reply_t *shm_reply_p = NULL;
    xcb_generic_error_t * error_p = NULL;
    error_s ret = {};
    shm_cookie = xcb_shm_query_version (xcb_conn_p);
    shm_reply_p = xcb_shm_query_version_reply (xcb_conn_p, shm_cookie, &error_p);
    if (error_p)
    {
        ret.error_str = "Can't find shm extension";
        ret.error_code = error_p->error_code;
    }

    if (shm_reply_p)
        free(shm_reply_p);
    if (error_p)
        free(error_p);
    return ret;
}

error_s
check_dri2 (xcb_connection_t *xcb_conn_p)
{
    error_s ret = {};
    xcb_generic_error_t *error_p = NULL;
    xcb_dri2_query_version_cookie_t xcb_dri2_ck =
            xcb_dri2_query_version (xcb_conn_p,
                                    XCB_DRI2_MAJOR_VERSION,
                                    XCB_DRI2_MINOR_VERSION);
    xcb_dri2_query_version_reply_t * xcb_dri2_reply_p =
            xcb_dri2_query_version_reply (xcb_conn_p,
                                          xcb_dri2_ck,
                                          &error_p);
    if (error_p)
    {
        ret.error_str = "Can't find dri2 extension";
        ret.error_code = error_p->error_code;
    }

    if (xcb_dri2_reply_p)
        free(xcb_dri2_reply_p);
    if (error_p)
        free(error_p);
    return ret;
}

rule_s
make_rule (xcb_connection_t *xcb_conn_p, xcb_screen_t *screen_p)
{
    rule_s ret = {};
    ret.count_frame_per_change = 2;
    ret.count_uniqes_frames = 10;
    ret.frame_per_second = 30;
    ret.max_width = screen_p->width_in_pixels;
    ret.max_height = screen_p->height_in_pixels;
    ret.dst_change = 1;
    ret.src_change = 0;
    ret.min_height = 100;
    ret.min_width = 100;
    ret.image_width = 640;
    ret.image_height = 480;
    ret.image_fourcc = FOURCC_SR32;
    return ret;
}

test_map_s
init_tests (void)
{
    test_map_s ret = {};
    ret.num_tests = 1;
    ret.test_case_array = calloc(ret.num_tests, sizeof(test_case_s));
    if (ret.test_case_array == NULL)
    {
        fprintf(stderr, "Can't alloc memory for test");
        ret.num_tests = 0;
        goto error_close;
    }

    ret.error_array = calloc(ret.num_tests, sizeof(error_s));
    if (ret.error_array == NULL)
    {
        fprintf(stderr, "Can't alloc memory for test");
        ret.num_tests = 0;
        goto error_close;
    }
    ret.test_case_array[0] = test_xv_resize_init();
    return ret;
error_close:
    if (ret.test_case_array)
        free(ret.test_case_array);
    if (ret.error_array)
        free(ret.error_array);

    return ret;
}

int main ()
{
    xcb_screen_iterator_t screen_iter;
    xcb_connection_t     *xcb_conn_p;
    const xcb_setup_t    *setup;
    xcb_screen_t         *screen_p;
    int                   screen_number;
    test_map_s tests = {};
    rule_s rule_p = {};
    error_s ret;
    /* getting the connection */
    xcb_conn_p = xcb_connect (NULL, &screen_number);
    if (!xcb_conn_p)
    {
        fprintf(stderr, "ERROR: can't connect to an X server\n");
    }
    ret = check_xv (xcb_conn_p);
    exit_if_fail (ret);
    ret = check_shm (xcb_conn_p);
    exit_if_fail (ret);
    ret = check_dri2 (xcb_conn_p);
    exit_if_fail (ret);
    DBG;
    /* getting the current screen */
    setup = xcb_get_setup (xcb_conn_p);
    screen_p = NULL;
    screen_iter = xcb_setup_roots_iterator (setup);
    for (; screen_iter.rem != 0; --screen_number, xcb_screen_next (&screen_iter))
        if (screen_number == 0)
        {
            screen_p = screen_iter.data;
            break;
        }
    if (!screen_p) {
        fprintf (stderr, "ERROR: can't get the current screen\n");
        xcb_disconnect (xcb_conn_p);
        return -1;
    }
    rule_p = make_rule (xcb_conn_p, screen_p);
    tests = init_tests();
    if (tests.num_tests == 0)
    {
        fprintf (stderr, "Nothing to test. num_tests = 0\n");
        return 0;
    }
    int i;
    DBG;
    for (i = 0; i < tests.num_tests; i++)
    {
        ret = tests.test_case_array[i].prepare_test(xcb_conn_p, screen_p, rule_p);
        if (ret.error_str)
        {
            tests.error_array[i] = ret;
            continue;
        }
        ret = tests.test_case_array[i].run_test(xcb_conn_p, screen_p, rule_p);
        if (ret.error_str)
        {
            tests.error_array[i] = ret;
            continue;
        }
        ret = tests.test_case_array[i].close_test(xcb_conn_p, screen_p, rule_p);
        if (ret.error_str)
        {
            tests.error_array[i] = ret;
            continue;
        }
    }
    for (i = 0; i < tests.num_tests; i++)
    {
        fprintf(stderr, "%s %d\n", tests.error_array[i].error_str,
                tests.error_array[i].error_code);
    }
    return 0;
}
