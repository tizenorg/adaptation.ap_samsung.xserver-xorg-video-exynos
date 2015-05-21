/*
 * hwa_sample.h
 *
 *  Created on: Mar 20, 2015
 *      Author: roma
 */

#ifndef HWA_SAMPLE_H_
#define HWA_SAMPLE_H_

/* Standart headers */
#include <stdio.h>
#include <stdlib.h>

#include <getopt.h>

/* X Server's headers */
#include <X11/Xlib.h>
#include <xcb/xcb.h>
#include <xcb/hwa.h>
#include <xcb/dri3.h>
#include <xcb/randr.h>

#define no_argument            0
#define required_argument      1
#define optional_argument      2

#define NUM_ARG 2
#define NUM_PARAMS 4

#define ON 1
#define OFF 1

#define PRINT( a )  ( printf("%d\n", a) )

void
fixed_xcb_str_next(xcb_str_iterator_t* it)
{
  it->data = (void*) ((char*) it->data + it->data->name_len + 1);
  --it->rem;
}

void xcb_init( void );

void help( void )
{
	printf( " \nUsage options:\n\n"
			"	--version        HWA query version\n"
			"	--info           list all extension and get some data about HWA \n"
			"	-c  --cursore        [0-1] 0:disable 1:enable\n"
			"	-r  --cursorr        [0-90-180-270-360] are angle degrees to be turned cursor.\n\n"
			"	-a  --accessibility  [0-1] 0:disable 1:enable.\n\n"
			"	-x  --scale          [0-1] [x:y:width:height] 0:disable 1:enable \n"
			"               	 	 if enable was chosen x:y:width:height must be specified.\n\n"
	        "            <-SHOW/HIDE Layer options:-> \n"
	        "	-s  --show          [0-1] 0: UI Layer, 1: XV Layer.\n"
			"	-h  --hide          [0-1] 0: UI Layer, 1: XV Layer.\n"
	);
}

void hwa_query_version( void );
void hwa_cursor_enable( int enable );
void hwa_cursor_rotate( int angle );
void hwa_accessibility( int enable );
void hwa_scale( int enable, char* scale_param );
void extensions_info( void );

int get_crtc_via_randr( char* output_to_find );
int* pars_params( char* scale_param );

void hwa_show_layer( int layer );
void hwa_hide_layer( int layer );






#endif /* HWA_SAMPLE_H_ */
