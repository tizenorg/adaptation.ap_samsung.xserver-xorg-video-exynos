#ifndef __MAIN_H__
#define __MAIN_H__

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

#include <pthread.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <xcb/xcb.h>

#include <xf86drm.h>
#include <tbm_bufmgr.h>

#include "xv_types.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#define max(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a > _b ? _a : _b; })
#define min(a,b) \
   ({ __typeof__ (a) _a = (a); \
       __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

#define exit_if_fail(error_s) \
    {if (error_s.error_str) {\
        fprintf (stderr, "Error: '%s' code: '%d'\n", \
                 error_s.error_str, error_s.error_code); \
     exit(-1);}}

#define C(b,m)              (((b) >> (m)) & 0xFF)
#define B(c,s)              ((((unsigned int)(c)) & 0xff) << (s))
#define FOURCC(a,b,c,d)     (B(d,24) | B(c,16) | B(b,8) | B(a,0))
#define FOURCC_STR(id)      C(id,0), C(id,8), C(id,16), C(id,24)
#define FOURCC_RGB565   FOURCC('R','G','B','P')
#define FOURCC_RGB32    FOURCC('R','G','B','4')
#define FOURCC_I420     FOURCC('I','4','2','0')
#define FOURCC_SR16     FOURCC('S','R','1','6')
#define FOURCC_SR32     FOURCC('S','R','3','2')
#define FOURCC_S420     FOURCC('S','4','2','0')

#define FOURCC_SN12     FOURCC('S','N','1','2')
#define XVIMAGE_SN12 \
   { \
    FOURCC_SN12, \
    XvYUV, \
    LSBFirst, \
    {'S','N','1','2', \
      0x00,0x00,0x00,0x10,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71}, \
    12, \
    XvPlanar, \
    2, \
    0, 0, 0, 0, \
    8, 8, 8, \
    1, 2, 2, \
    1, 2, 2, \
    {'Y','U','V', \
      0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}, \
    XvTopToBottom \
   }

#define DBG \
{printf("%s %d\n", __FUNCTION__, __LINE__);}

typedef struct
{
    uint32_t  min_width;
    uint32_t  min_height;
    uint32_t  max_width;
    uint32_t  max_height;
    int src_change;
    int dst_change;
    uint32_t image_width;
    uint32_t image_height;
    uint32_t image_fourcc;
    size_t count_frame_per_change;
    size_t frame_per_second;
    size_t count_uniqes_frames;
    /* TODO: More rules */
} rule_s;

typedef struct
{
    uint8_t error_code;
    char const * error_str;
} error_s;

typedef error_s (*prepare_test_case) (xcb_connection_t *, xcb_screen_t *, rule_s);
typedef error_s (*run_test_case) (xcb_connection_t *, xcb_screen_t *, rule_s);
typedef error_s (*close_test_case)(xcb_connection_t *, xcb_screen_t *, rule_s);

typedef struct _test_case_s
{
    prepare_test_case prepare_test;
    run_test_case run_test;
    close_test_case close_test;
} test_case_s;
#endif
