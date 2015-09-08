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

#ifndef SEC_MEMORY_H
#define SEC_MEMORY_H

#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <X11/Xdefs.h>	/* for Bool */

#define GETSP() ({ unsigned int sp; asm volatile ("mov %0,sp " : "=r"(sp)); sp;})
#define BUF_SIZE				256
#define _PAGE_SIZE       		(1 << 12)
#define _ALIGN_UP(addr,size)    (((addr)+((size)-1))&(~((size)-1)))
#define _ALIGN_DOWN(addr,size)  ((addr)&(~((size)-1)))
#define PAGE_ALIGN(addr)        _ALIGN_DOWN(addr, _PAGE_SIZE)

static inline void secMemoryStackTrim (void)
{
    unsigned int sp;
    char buf[BUF_SIZE];
    FILE* file;
    unsigned int stacktop;
    int found = 0;

    sp = GETSP();

    snprintf (buf, BUF_SIZE, "/proc/%d/maps", getpid());
    file = fopen (buf,"r");

    if (!file)
        return;

    while (fgets (buf, BUF_SIZE, file) != NULL)
    {
        if (strstr (buf, "[stack]"))
        {
            found = 1;
            break;
        }
    }

    fclose (file);

    if (found)
    {
        sscanf (buf,"%x-",&stacktop);
        if (madvise ((void*)PAGE_ALIGN (stacktop), PAGE_ALIGN (sp)-stacktop, MADV_DONTNEED) < 0)
            perror ("stack madvise fail");
    }
}

Bool secMemoryInstallHooks (void);
Bool secMemoryUninstallHooks (void);

#endif/* SEC_MEMORY_H */
