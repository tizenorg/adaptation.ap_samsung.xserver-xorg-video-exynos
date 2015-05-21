
#include <xf86drm.h>

#include "dri2_dri3.h"

typedef struct display_t
{
    xcb_connection_t* dpy;
    xcb_connection_t* client_dpy;
    xcb_screen_t* screen;
    xcb_window_t wnd;
} display_t;

typedef struct graphics_ctx_t
{
    int mode;
    draw_funcs_t draw_funcs;
    uint32_t num_of_bufs;
} graphics_ctx;

extern display_t display;
extern graphics_ctx gr_ctx;
extern pthread_t thread_id;

extern xcb_rectangle_t wnd_pos;

void get_wnd_geometry (xcb_window_t win_id, xcb_rectangle_t* geometry);
void set_error (const char* str);

int prepare_dri2_ext (void);
void dri2_loop (void);

int prepare_dri3_present_ext (void);
void dri3_loop (void);
