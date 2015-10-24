/**************************************************************************

 fps computation module's source

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

#include <stdio.h>

#include "fps.h"

#define BILLION   1000000000
#define BILLION_F 1000000000.0f

#define FPS_AVERAGE 1		// fps average value (in seconds)

// for fps calculation
//=====================================================================================
void
fps_calc (const struct timespec* start)
{
	struct timespec end;
	double time_diff;
	double fps;

	static double sum_fps;
	static time_t prev_time;
	static int cnt = 1;

    if (!start) return;

	// note: start variable contains start time of time-execution measured code section

	// get time end of time-execution measured code section
    clock_gettime (CLOCK_REALTIME, &end);

	// get difference in nanoseconds
	time_diff = (double)( ( end.tv_sec - start->tv_sec ) * BILLION +
												( end.tv_nsec - start->tv_nsec ) );

	// get frames per second value
	fps = BILLION_F / time_diff;

	// average fps calculation
    if (end.tv_sec - prev_time >= FPS_AVERAGE)
	{
        printf ("\033[8D");
        printf ("        ");
        printf ("\033[8D");
        printf ("%8.1f", sum_fps/cnt);
        fflush (stdout);

		cnt = 0;
		sum_fps = 0;

		prev_time = end.tv_sec;
	}
	else
	{
		cnt++;
		sum_fps += fps;	
	}
}

// return time diff (between time in 'start' and time when this function is called)
// in nanoseconds
//=====================================================================================
double
get_time_diff (const struct timespec* start)
{
    struct timespec end;
    double time_diff;

    if (!start) return 0;

    // note: start variable contains start time of time-execution measured code section

    // get time end of time-execution measured code section
    clock_gettime (CLOCK_REALTIME, &end);

    // get difference in nanoseconds
    time_diff = (double)( ( end.tv_sec - start->tv_sec ) * BILLION +
                                                ( end.tv_nsec - start->tv_nsec ) );
    return time_diff;
}
