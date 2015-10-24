/**************************************************************************

xserver-xorg-video-exynos

Copyright 2011 Samsung Electronics co., Ltd. All Rights Reserved.

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

#ifndef __SEC_PROP_H__
#define __SEC_PROP_H__

Bool exynosPropSetLvdsFunc(xf86OutputPtr output, Atom property,
                           RRPropertyValuePtr value);
void exynosPropUnSetLvdsFunc(xf86OutputPtr pOutput);

Bool exynosPropSetDisplayMode(xf86OutputPtr output, Atom property,
                              RRPropertyValuePtr value);
void exynosPropUnSetDisplayMode(xf86OutputPtr pOutput);

Bool exynosPropSetFbVisible(xf86OutputPtr output, Atom property,
                            RRPropertyValuePtr value);
Bool exynosPropSetVideoOffset(xf86OutputPtr output, Atom property,
                              RRPropertyValuePtr value);
Bool exynosPropSetScreenRotate(xf86OutputPtr pOutput, Atom property,
                               RRPropertyValuePtr value);

Bool exynosPropFbVisible(char *cmd, Bool always, RRPropertyValuePtr value,
                         ScrnInfoPtr scrn);
Bool exynosPropVideoOffset(char *cmd, RRPropertyValuePtr value,
                           ScrnInfoPtr scrn);
Bool exynosPropScreenRotate(char *cmd, RRPropertyValuePtr value,
                            ScrnInfoPtr scrn);

#endif                          /* __SEC_PROP_H__ */
