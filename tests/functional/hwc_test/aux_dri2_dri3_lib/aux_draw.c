#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "dri2_dri3.h"

// set pixel with 'color' in 'buf' on (x, y) position
// no memory bound out checks, do it within caller !!!
//========================================================================
static inline void
draw_point (int x, int y, void* buf, uint16_t stride, unsigned int color)
{
    *((int*)(buf + x*(BPP/8) + y*stride)) = color;
}

// draws line(s) into bo (Bresenham algorithm)
// points_num - number of elements of xcb_point_t array
// points - array of points, which will be used to draw line(s)
// line(s) is(are) drawn between each pair of points
// any attempts to draw line(s), which is(are) out of pixmap/bo size are rejected
//=========================================================================
int raw_draw_line (const bo_t* bo_e, uint32_t points_num, const xcb_point_t* points,
                   unsigned int color)
{
    tbm_bo bo = NULL;
    tbm_bo_handle hndl;
    uint16_t stride;
    uint16_t width, height;

    int x, y, x1, x2, y1, y2;
    int dx, dy, error, direct, invert;
    int temp, i;

    if (!points || points_num < 2 || !bo_e)
    {
        printf ("raw_draw_line: invalid input parameters.\n");
        return 1;
    }

    hndl.ptr = NULL;
    bo = bo_e->bo;
    stride = bo_e->stride;
    width = bo_e->width;
    height = bo_e->height;

    hndl = tbm_bo_map (bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    if (!hndl.ptr)
    {
        printf ("raw_draw_line: Error while tbm_bo_map.\n");
        exit (1);
    }

    // x1, y1 - first point  (in pixels)
    // x2, y2 - second point (in pixels)
    // x grows from left to right
    // y grows from up to down
    // draw (points_num - 1) line(s)
    for (i = 0; i < points_num - 1; i++)
    {
        x1 = points->x;
        y1 = points++->y;
        x2 = points->x;
        y2 = points->y;

        if (x1 < 0  || x2 < 0 || y1 < 0 || y2 < 0)
        {
            printf ("raw_draw_line: invalid input parameters.\n");
            continue;
        }

        if (x1 > (width - 1) || x2 > (width - 1) || y1 > (height - 1) || y2 > (height - 1))
        {
            printf ("raw_draw_line: you are beyond image boundaries:\n"
                    " x1 = %d, y1 = %d, x2 = %d, y2 = %d, \twidth = %hu, height = %hu\n",
                    x1, y1, x2, y2, width, height);
            continue;
        }

        // to draw vertical lines
        if (x1 == x2)
        {
            // we bypass image from bottom to top coordinates, so we must
            // swap points if it necessary
            if (y2 < y1)
            {
                temp = y1;
                y1 = y2;
                y2 = temp;
            }

            for (y = y1; y <= y2; y++)
                draw_point (x1, y, hndl.ptr, stride, color);

            continue;
        }

        // if y-component of line is changed quickly then x-component,
        // we must exchange axis to use Bresenham algorithm more effectively,
        // due to that we have for cycle from x1 to x2, therefore we can change
        // body of for cycle insted exchange axis, but this's not pretty
        invert = abs (y2 - y1) > abs (x2 - x1);
        if (invert)
        {
            temp = x1;
            x1 = y1;
            y1 = temp;

            temp = x2;
            x2 = y2;
            y2 = temp;
        }

        // algorithm can draw line only if x of first point is less
        // then x of second point, so we simple swap points if it's necessary
        if (x1 > x2)
        {
            temp = x1;
            x1 = x2;
            x2 = temp;

            temp = y1;
            y1 = y2;
            y2 = temp;
        }

        // where do we go while draw line: up(or straight) or down
        if (y2 < y1)
            direct = -1;
        else
            direct = 1;

        error = 0;
        dx = x2 - x1;
        dy = abs(y2 - y1);

        // to draw another cases of line (it's Bresenham algorithm implementation,
        // which supports varios line's directions)
        for (x = x1, y = y1; x <= x2; x++)
        {
            draw_point (invert ? y : x, invert ? x : y, hndl.ptr, stride, color);

            error += dy;

            if (error * 2 >= dx)
            {
                y += direct;
                error -= dx;
            }
        }
    } // for (i = 0; i < points_num - 1; i++)

    tbm_bo_unmap (bo);

    return 0;
}

// draw filled rect into bo
// rect_num - amount of rectangles to draw (in current implemantation must be '1')
// rects - array of rectangles to draw
// any attempts to draw rectangle, which is out of pixmap/bo size are rejected
// draw only one rectangle !!!
//=========================================================================
int raw_fill_rect (const bo_t* bo_e, uint32_t rect_num, const xcb_rectangle_t* rects,
                   unsigned int color)
{
    tbm_bo bo = NULL;
    tbm_bo_handle hndl;
    uint16_t height;
    uint16_t width;
    uint16_t stride;
    int i, j;

    if (
        rects->x < 0 || rects->y < 0 || rects->width <= 0 || rects->height <= 0  ||
        !rect_num || rect_num > 1 || !rects || !bo_e
        )
    {
        printf ("raw_fill_rect: invalid input parameters.\n");
        return 1;
    }

    bo = bo_e->bo;
    stride = bo_e->stride;
    width = bo_e->width;
    height = bo_e->height;

    if (rects->x + rects->width > width || rects->y + rects->height > height)
    {
        printf ("raw_fill_rect: you are beyond image boundaries:\n"
                " x = %d, y = %d, width = %d, height = %d, widht_bo = %hu, height_bo = %hu.\n",
                rects->x, rects->y, rects->width, rects->height, width, height);
        return 1;
    }

    hndl = tbm_bo_map (bo, TBM_DEVICE_CPU, TBM_OPTION_WRITE);
    if (!hndl.ptr)
    {
        printf ("raw_fill_rect: Error while tbm_bo_map.\n");
        exit (1);
    }

    // this code works quikly then below code.
    // if caller wants to clear (black color) all bo.
    // another colors cann't be used due to memset semantic
    // (memset use only low byte of second parameter)
    if (width == rects->width && height == rects->height && !color)
    {
        memset (hndl.ptr, 0, bo_e->stride * bo_e->height);
        goto success;
    }

    // x grows from left to right
    // y grows from up to down

    // shift to pixel, from which we must draw
    hndl.ptr += rects->x*(BPP/8) + rects->y*stride;

    // memory boundaries were checked above
    for (i = 0; i < rects->height; i++)
    {
        for (j = 0; j < rects->width; j++)
        {
            *((int*)hndl.ptr) = (int)color;
            hndl.ptr += (BPP/8);
        }

        // shift to pixel, from which we must draw
        hndl.ptr -= j*(BPP/8) - stride;
    }

success:
    tbm_bo_unmap (bo);

    return 0;
}
