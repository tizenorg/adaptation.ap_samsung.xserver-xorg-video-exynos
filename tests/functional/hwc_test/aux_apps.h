/**************************************************************************

 auxilary module's header

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

#ifndef _AUX_APPS_H
#define _AUX_APPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <xcb/xcb.h>

void cmd_parse (int argc, char* const argv[]);
xcb_window_t create_window (xcb_connection_t* dpy, uint32_t background, uint32_t root_parent, char* name);

#ifdef __cplusplus
}
#endif

#endif
