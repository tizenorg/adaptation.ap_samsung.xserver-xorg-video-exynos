#ifndef _FPS_H
#define _FPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <time.h>

void fps_calc (struct timespec* start);
double get_time_diff (struct timespec* start);

#ifdef __cplusplus
}
#endif

#endif
