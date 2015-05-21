
#include <stdio.h>

#include "fps.h"

#define BILLION   1000000000
#define BILLION_F 1000000000.0f

#define FPS_AVERAGE 1		// fps average value (in seconds)

// for fps calculation
//=====================================================================================
void
fps_calc (struct timespec* start)
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
get_time_diff (struct timespec* start)
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
