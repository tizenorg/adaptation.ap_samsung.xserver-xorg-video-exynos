/**************************************************************************

xserver-xorg-video-exynos

Copyright 2010 - 2011 Samsung Electronics co., Ltd. All Rights Reserved.

Contact: SooChan Lim <sc1.lim@samsung.com>

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

#include "neonmem.h"

//#define _USE_LIBC
//#define _X86_SSE2 // may not work on linux platform
#define _ARM_NEON

#ifdef _USE_LIBC
#include <string.h>
#endif

//////////////////////////////////////////////////////////////////////////
// General C implementation of copy functions
//////////////////////////////////////////////////////////////////////////
static inline void memcpy32 (unsigned long *dst, unsigned long *src, int size)
{
    while ( size > 0 )
    {
        *dst++ = *src++;
        size--;
    }
}

static inline void memcpy_forward_32 (unsigned long *dst, unsigned long *src, int size)
{
    while ( size > 0 )
    {
        *dst++ = *src++;
        size--;
    }
}

static inline void memcpy_backward_32 (unsigned long *dst, unsigned long *src, int size)
{
    dst = dst + size;
    src = src + size;

    while ( size > 0 )
    {
        *(--dst) = *(--src);
        size--;
    }
}

static inline void memcpy_forward_16 (unsigned short *dst, unsigned short *src, int size)
{
    while ( size > 0 )
    {
        *dst++ = *src++;
        size--;
    }
}

static inline void memcpy_backward_16 (unsigned short *dst, unsigned short *src, int size)
{
    dst = dst + size;
    src = src + size;

    while ( size > 0 )
    {
        *(--dst) = *(--src);
        size--;
    }
}

static inline void memcpy_forward (unsigned char *dst, unsigned char *src, int size)
{
    while ( size > 0 && ((unsigned long) dst & 3))
    {
        *(unsigned short*)dst = *(unsigned short*)src;
        dst += 2;
        src += 2;
        size -= 2;
    }

    while ( size >= 4 )
    {
        *(unsigned long*)dst = *(unsigned long*)src;
        dst += 4;
        src += 4;
        size -= 4;
    }

    while ( size > 0 )
    {
        *dst++ = *src++;
        size--;
    }
}

static inline void memcpy_backward (unsigned char *dst, unsigned char *src, int size)
{
    dst = dst + size;
    src = src + size;

    while ( size > 0 && ((unsigned long) dst & 3))
    {
        *(--dst) = *(--src);
        size--;
    }

    while ( size >= 4 )
    {
        dst -= 4;
        src -= 4;
        size -= 4;
        *(unsigned long*)dst = *(unsigned long*)src;
    }

    while ( size > 0 )
    {
        *(--dst) = *(--src);
        size--;
    }
}

//////////////////////////////////////////////////////////////////////////
// ARM assembly implementation of copy functions
//////////////////////////////////////////////////////////////////////////
#ifdef _ARM_ASM
static inline void memcpy_forward (unsigned char *dst, unsigned char *src, int size)
{

}

static inline void memcpy_backward (unsigned char *dst, unsigned char *src, int size)
{

}
#endif

//////////////////////////////////////////////////////////////////////////
// ARM NEON implementation of copy functions
//////////////////////////////////////////////////////////////////////////
#ifdef _ARM_NEON
extern void memcpy_forward_32_neon (unsigned long *dst, unsigned long *src, int size);
extern void memcpy_backward_32_neon (unsigned long *dst, unsigned long *src, int size);
extern void memcpy_forward_16_neon (unsigned short *dst, unsigned short *src, int size);
extern void memcpy_backward_16_neon (unsigned short *dst, unsigned short *src, int size);
#endif

//////////////////////////////////////////////////////////////////////////
// X86 SSE2 copy functions
//////////////////////////////////////////////////////////////////////////
#ifdef _X86_SSE2
#include <emmintrin.h>

static inline void memcpy_forward_sse2 (unsigned char *dst, unsigned char *src, int size)
{
    while ( size > 0 && ((unsigned long) dst & 1))
    {
        *dst++ = *src++;
        size--;
    }

    while ( size > 0 && ((unsigned long) dst & 3))
    {
        *(unsigned short*)dst = *(unsigned short*)src;
        dst += 2;
        src += 2;
        size -= 2;
    }

    while ( size > 0 && ((unsigned long) dst & 63))
    {
        *(unsigned long*)dst = *(unsigned long*)src;
        dst += 4;
        src += 4;
        size -= 4;
    }

    if ((reinterpret_cast<unsigned long> (src) & 15) == 0 )
    {
        while ( size >= 64 )
        {
            __m128 xmm0, xmm1, xmm2, xmm3;

            //_mm_prefetch((const char*)(src+320), _MM_HINT_NTA);

            xmm0 = _mm_load_ps ((float*)(src));
            xmm1 = _mm_load_ps ((float*)(src + 16));
            xmm2 = _mm_load_ps ((float*)(src + 32));
            xmm3 = _mm_load_ps ((float*)(src + 48));

            _mm_stream_ps ((float*)(dst), xmm0);
            _mm_stream_ps ((float*)(dst + 16), xmm1);
            _mm_stream_ps ((float*)(dst + 32), xmm2);
            _mm_stream_ps ((float*)(dst + 48), xmm3);

            src += 64;
            dst += 64;
            size -= 64;
        }

        while ( size >= 16 )
        {
            _mm_stream_ps ((float*)dst, _mm_load_ps ((float*)src));
            dst += 16;
            src += 16;
            size -= 16;
        }
    }
    else
    {
        while ( size >= 64 )
        {
            __m128 xmm0, xmm1, xmm2, xmm3;

            //_mm_prefetch((const char*)(src+320), _MM_HINT_NTA);

            xmm0 = _mm_loadu_ps ((float*)(src));
            xmm1 = _mm_loadu_ps ((float*)(src + 16));
            xmm2 = _mm_loadu_ps ((float*)(src + 32));
            xmm3 = _mm_loadu_ps ((float*)(src + 48));

            _mm_stream_ps ((float*)(dst), xmm0);
            _mm_stream_ps ((float*)(dst + 16), xmm1);
            _mm_stream_ps ((float*)(dst + 32), xmm2);
            _mm_stream_ps ((float*)(dst + 48), xmm3);

            src += 64;
            dst += 64;
            size -= 64;
        }

        while ( size >= 16 )
        {
            _mm_stream_ps ((float*)dst, _mm_loadu_ps ((float*)src));
            dst += 16;
            src += 16;
            size -= 16;
        }
    }

    while ( size >= 4 )
    {
        *(unsigned long*)dst = *(unsigned long*)src;
        dst += 4;
        src += 4;
        size -= 4;
    }

    while ( size > 0 )
    {
        *dst++ = *src++;
        size--;
    }
}

static inline void memcpy_backward_sse2 (unsigned char *dst, unsigned char *src, int size)
{
    dst = dst + size;
    src = src + size;

    while ( size > 0 && ((unsigned long) dst & 1))
    {
        dst--;
        src--;
        size--;
        *dst = *src;
    }

    while ( size > 0 && ((unsigned long) dst & 3))
    {
        dst -= 2;
        src -= 2;
        size -= 2;
        *(unsigned short*)dst = *(unsigned short*)src;
    }

    while ( size > 0 && ((unsigned long) dst & 63))
    {
        dst -= 4;
        src -= 4;
        size -= 4;
        *(unsigned long*)dst = *(unsigned long*)src;
    }

    if ((reinterpret_cast<unsigned long> (src) & 15) == 0 )
    {
        while ( size >= 64 )
        {
            __m128 xmm0, xmm1, xmm2, xmm3;

            src -= 64;
            dst -= 64;
            size -= 64;

            //_mm_prefetch((const char*)(src+16), _MM_HINT_NTA);

            xmm0 = _mm_load_ps ((float*)(src));
            xmm1 = _mm_load_ps ((float*)(src + 16));
            xmm2 = _mm_load_ps ((float*)(src + 32));
            xmm3 = _mm_load_ps ((float*)(src + 48));

            _mm_stream_ps ((float*)(dst), xmm0);
            _mm_stream_ps ((float*)(dst + 16), xmm1);
            _mm_stream_ps ((float*)(dst + 32), xmm2);
            _mm_stream_ps ((float*)(dst + 48), xmm3);
        }

        while ( size >= 16 )
        {
            dst -= 16;
            src -= 16;
            size -= 16;
            _mm_stream_ps ((float*)dst, _mm_load_ps ((float*)src));
        }
    }
    else
    {
        while ( size >= 64 )
        {
            __m128 xmm0, xmm1, xmm2, xmm3;

            src -= 64;
            dst -= 64;
            size -= 64;

            //_mm_prefetch((const char*)(src+16), _MM_HINT_NTA);

            xmm0 = _mm_loadu_ps ((float*)(src));
            xmm1 = _mm_loadu_ps ((float*)(src + 16));
            xmm2 = _mm_loadu_ps ((float*)(src + 32));
            xmm3 = _mm_loadu_ps ((float*)(src + 48));

            _mm_stream_ps ((float*)(dst), xmm0);
            _mm_stream_ps ((float*)(dst + 16), xmm1);
            _mm_stream_ps ((float*)(dst + 32), xmm2);
            _mm_stream_ps ((float*)(dst + 48), xmm3);
        }

        while ( size >= 16 )
        {
            dst -= 16;
            src -= 16;
            size -= 16;
            _mm_stream_ps ((float*)dst, _mm_loadu_ps ((float*)src));
        }
    }

    while ( size >= 4 )
    {
        dst -= 4;
        src -= 4;
        size -= 4;
        *(unsigned long*)dst = *(unsigned long*)src;
    }

    while ( size > 0 )
    {
        dst--;
        src--;
        size--;
        *dst = *src;
    }
}
#endif

static inline void move_memory_32 (unsigned long *dst, unsigned long *src, int size)
{
#ifdef _USE_LIBC
    memmove (dst, src, size*4);
#elif defined(_ARM_NEON)
    if ( dst > src && dst < src + size )
        memcpy_backward_32_neon (dst, src, size);
    else
        memcpy_forward_32_neon (dst, src, size);
#else
    if ( dst > src && dst < src + size )
        memcpy_backward_32 (dst, src, size);
    else
        memcpy_forward_32 (dst, src, size);
#endif
}

static inline void move_memory_16 (unsigned short *dst, unsigned short *src, int size)
{
#ifdef _USE_LIBC
    memmove (dst, src, size*2);
#elif defined(_ARM_NEON)
    if ( dst > src && dst < src + size )
        memcpy_backward_16_neon (dst, src, size);
    else
        memcpy_forward_16_neon (dst, src, size);
#else
    if ( dst > src && dst < src + size )
        memcpy_backward_16 (dst, src, size);
    else
        memcpy_forward_16 (dst, src, size);
#endif
}

/**
 *	@brief Pixel block move function within one image buffer
 *
 *	@param bits			buffer address of top-left corner
 *	@param bpp			bits per pixel, must be one of 16 and 32
 *	@param stride		number of bytes to go next row (can be negative)
 *	@param img_width	entire image width
 *	@param img_height	entire image height
 *	@param sx			top-left x position of source area
 *	@param sy			top-left y position of source area
 *	@param dx			top-left x position of destination area
 *	@param dy			top-left y position of destination area
 *	@param w			width of area to copy
 *	@param h			height of area to copy
 *
 *	@remark	This function supports overlapping in source and destination area.
 *			Any source or destination area which is outside given image will be cropped.
 *			Support negative stride.
 *			bits must be word aligned if bpp == 32 and half-word aligned if bpp == 16.
 */
int move_pixels (void *bits, int bpp, int stride, int img_width, int img_height,
                 int sx, int sy, int dx, int dy, int w, int h)
{
    int bytes_per_pixel;
    unsigned char *src_row;
    unsigned char *dst_row;


    //////////////////////////////////////////////////////////////////////////
    // check validity of arguments
    //////////////////////////////////////////////////////////////////////////
    if ( !bits || img_width < 0 || img_height < 0 || w < 0 || h < 0 )
        return 0;

    if ( bpp == 32 )
        bytes_per_pixel = 4;
    else if ( bpp == 16 )
        bytes_per_pixel = 2;
    else // unsupported bpp
        return 0;

    if ( bytes_per_pixel*img_width < stride ) // invalid image
        return 0;

    // Wow thanks, we have nothing to do
    if ( sx == dx && sy == dy )
        return 1;


    //////////////////////////////////////////////////////////////////////////
    // Bounds check and cropping
    //////////////////////////////////////////////////////////////////////////
    while ( sx < 0 || dx < 0 )
    {
        sx++;
        dx++;
        w--;
    }

    while ( sy < 0 || dy < 0 )
    {
        sy++;
        dy++;
        h--;
    }

    while ( sx + w > img_width || dx + w > img_width )
        w--;

    while ( sy + h > img_height || dy + h > img_height )
        h--;


    //////////////////////////////////////////////////////////////////////////
    // Check overlap and do copy
    //////////////////////////////////////////////////////////////////////////

    // No remaining area to copy
    if ( w <= 0 || h <= 0 )
        return 1;

    src_row = (unsigned char*)bits + sy*stride + sx*bytes_per_pixel;
    dst_row = (unsigned char*)bits + dy*stride + dx*bytes_per_pixel;

    // Check if we need to reverse y order
    if ( dy > sy && dy < sy + h )
    {
        src_row += (h - 1) *stride;
        dst_row += (h - 1) *stride;
        stride = -stride;
    }

    if ( bpp == 32 )
    {
        while ( h-- )
        {
            move_memory_32 ((unsigned long*)dst_row, (unsigned long*)src_row, w);
            dst_row += stride;
            src_row += stride;
        }
    }
    else if ( bpp == 16 )
    {
        while ( h-- )
        {
            move_memory_16 ((unsigned short*)dst_row, (unsigned short*)src_row, w);
            dst_row += stride;
            src_row += stride;
        }
    }

    return 1;
}
