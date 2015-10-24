#ifndef __MAIN_H__
#define __MAIN_H__

//#include <X11/X.h>
#include <X11/Xlib.h>
//#include <X11/Xutil.h>
//#include <X11/Xatom.h>
//#include <X11/Xos.h>
//#include <X11/cursorfont.h>
//#include <X11/extensions/hwc.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>

#define AMOUNT_OF_PLANES 4

// hwc_set values:
// 1 - set pixmap(s)
// 2 - set window(s) (internal or external)
// 3 - loop
// 4 - move window
// 5 - resize window

typedef struct _ThreadData
{
    //Pixmap pixmap;
    int width;
    int height;

    pthread_t thread;
} ThreadData;

struct app_info
{
    int maxLayer;
    int count;
    int wnd_to_change;
    //Drawable draws[AMOUNT_OF_PLANES];
    //XRectangle dst[AMOUNT_OF_PLANES];
    //XRectangle src[AMOUNT_OF_PLANES];
    int hwc_set;    // picked work mode
    int use_root;
    int hwc_unset;
    int hwc_loop;
    int redirect;   // whether X server does redirect to each created window or not
    int not_use_hw_layers; // whether we want to use hardware layers to make compositing or not
    int set_all_ext_wnds;  // whether we want to use all existing external windows or not
    int set_spec_ext_wnds; // only specified wnds
    int num_of_ext_wnds;   // amount of all/specified external windows to want to set
    //Window* ext_wnd_ids;   // array of specified external windows id's
};

extern struct app_info ai;
//extern Window root, change_size, move_btn, change_stack_order;
extern int wd1, ht1;
extern int dispsize_width, dispsize_height;

extern void* thread1_hwcsample (void *data);
void change_focus (void);
void hwc_set (void);
void hwc_movew (void);
void hwc_resizew (void);

int launch_clients (char* clients[], int num_of_clients);

#endif
