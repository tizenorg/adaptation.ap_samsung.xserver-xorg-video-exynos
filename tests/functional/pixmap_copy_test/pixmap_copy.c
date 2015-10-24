#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#define  ERROR(str, arg...)       \
	do                            \
	{                             \
		printf(str, ##arg);       \
		exit(1);                  \
	}                             \
	while(0)                      \

#define WIDTH 720
#define HEIGHT 1280

extern int errno;

int main(int argc, char **argv)
{
    Display  *display;
    Window    window;
    Visual   *visual;
    Pixmap    pixmap, pixmap2;
    GC        gr_context;
    XGCValues gr_values;
    XSetWindowAttributes attributes;
    int depth  = 0;
    int screen = 0;
    int num    = 0;
    time_t start, current;

/* Connect to X Server */
    if ((display = XOpenDisplay(NULL)) == NULL)
    	ERROR("Can't connect X server: %s\n", strerror(errno));

/* Get default screen, visual etc */
    screen = XDefaultScreen(display);
    visual = XDefaultVisual(display,screen);
	depth  = XDefaultDepth(display,screen);
	attributes.background_pixel = XWhitePixel(display,screen);

	printf("depth %d\n", depth);

/* Create window */
	window = XCreateWindow(display, XRootWindow(display,screen),
						0, 0, WIDTH, HEIGHT, 0, depth,  InputOutput,
						visual , CWBackPixel, &attributes);

/* Chose events we are intrested in */
	XSelectInput(display, window, ExposureMask | KeyPressMask);
	XFlush(display);

/* Create pxmap */
	pixmap = XCreatePixmap(display, window, WIDTH, HEIGHT, depth);

/* Create graphic contexts */
	gr_values.foreground = 0xAABBCC;
	gr_context = XCreateGC(display, pixmap, GCForeground, &gr_values);

	XFillRectangle(display, pixmap, gr_context, 0, 0, WIDTH, HEIGHT);
	XCopyArea(display, pixmap, window, gr_context, 0, 0, WIDTH, HEIGHT,  0, 0);

/* Display the window on screen*/
    XMapWindow(display, window);
    XFlush(display);


/*--------------- pixmap -> window -----------------*/

    printf("-------------------------\n");
    printf("wait 2\n");
    sleep(2);
    printf("start\n\n");

    num = 0;
    start = time(NULL);
    while (time(&current) < start + 5)
    {
        XCopyArea(display, pixmap, window, gr_context, 0, 0, WIDTH, HEIGHT,  0, 0);
        ++num;
        XFlush(display);
    }

    printf("pixmap -> window: %d\n", num);
    printf("-------------------------\n\n");

    pixmap2 = XCreatePixmap(display, window, WIDTH, HEIGHT, depth);
    XFillRectangle(display, pixmap2, gr_context, 0, 0, WIDTH, HEIGHT);
    XFlush(display);


/*--------------- pixmap -> pixmap -----------------*/

    printf("-------------------------\n");
    printf("wait 2\n");
    sleep(2);
    printf("start\n\n");

    num = 0;
    start = time(NULL);
	while (time(&current) < start + 5)
	{
		XCopyArea(display, pixmap, pixmap2, gr_context, 0, 0, WIDTH, HEIGHT,  0, 0);
		++num;
		XFlush(display);
	}

	printf("pixmap -> pixmap: %d\n", num);
	printf("-------------------------\n");


/* Close connection with X */
    XCloseDisplay(display);

    return 0;
}

