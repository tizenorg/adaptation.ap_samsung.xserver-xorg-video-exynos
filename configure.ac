
AC_PREREQ(2.61)
AC_INIT([xserver-xorg-video-exynos],
        1.0.0,
        [https://],
        xserver-xorg-video-exynos)

AC_CONFIG_SRCDIR([Makefile.am])
AM_CONFIG_HEADER([config.h])
AC_CONFIG_AUX_DIR(.)

AM_INIT_AUTOMAKE([dist-bzip2 subdir-objects])

AM_MAINTAINER_MODE

# Checks for programs.
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CC
AC_PROG_CC_STDC
AM_PROG_AS
AM_PROG_CC_C_O

AH_TOP([#include "xorg-server.h"])

AC_ARG_WITH(xorg-module-dir,
            AC_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
						   [moduledir="$withval"],
						   [moduledir="$libdir/xorg/modules"])

AC_ARG_ENABLE(pciaccess,
	      AS_HELP_STRING([--enable-pciaccess],
                             [Enable use of libpciaccess (default: disabled)]),
			     [PCIACCESS=$enableval], [PCIACCESS=no])

AC_ARG_ENABLE(dri3,
	      AS_HELP_STRING([--enable-dri3],
                             [Enable DRI3 backend (default: disabled)]),
			     [DRI3=$enableval], [DRI3=no])
			     
AC_ARG_ENABLE(hwc,
          AS_HELP_STRING([--enable-hwc],
                             [Enable support of HWC extesion(default: disabled)]),
                 [HWC=$enableval], [HWC=no])
			     
AC_ARG_ENABLE(hwa,
          AS_HELP_STRING([--enable-hwa],
                             [Enable support of HWA extesion(default: disabled)]),
                 [HWA=$enableval], [HWA=no])

AC_ARG_ENABLE(legacy-interface,
	      AS_HELP_STRING([--enable-legacy-interface],
                             [Enable using of legacy kernel interface (default: disabled)]),
			     [LEGACY_INTERFACE=$enableval], [LEGACY_INTERFACE=no])

AC_ARG_ENABLE(ftests,
AS_HELP_STRING([--enable-ftests], [Enable functional tests (default: no)]),
                [FT="$enableval"], [FT="no"])
# Checks for extensions
PKG_CHECK_MODULES(XORG, xorg-server)
XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
XORG_DRIVER_CHECK_EXT(RENDER, renderproto)
XORG_DRIVER_CHECK_EXT(XV, videoproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(PROTO, [xproto fontsproto])

sdkdir=$(pkg-config --variable=sdkdir xorg-server)

# check the conditions
PKG_CHECK_MODULES(XDBG, [xdbg], [xdbg=yes], [xdbg=no])
PKG_CHECK_MODULES(DRM, libdrm)
PKG_CHECK_MODULES(LIBTBM, libtbm)
PKG_CHECK_MODULES(UDEV, [libudev], [udev=yes], [udev=no])
PKG_CHECK_MODULES(PNG, [libpng])

if test x"$xdbg" = xno; then
    AC_MSG_ERROR([Not find xdbg lib])
fi

if test x"$udev" = xyes; then
	AC_DEFINE(HAVE_UDEV,1,[Enable udev-based monitor hotplug detection])
fi

# ttrace support
PKG_CHECK_MODULES(TTRACE, [ttrace], [HAVE_TTRACE=yes], [HAVE_TTRACE=no])
if test x"$HAVE_TTRACE" = xyes; then
    AC_DEFINE(ENABLE_TTRACE, 1, [ttrace available])
    AC_SUBST([ENABLE_TTRACE])
fi


EXYNOS_CFLAGS="$EXYNOS_CFLAGS $PROTO_CFLAGS $XDBG_CFLAGS $DRM_CFLAGS $LIBTBM_CFLAGS $UDEV_CFALGS $PNG_CFLAGS"
EXYNOS_LIBS="$EXYNOS_LIBS $XDBG_LIBS $DRM_LIBS $LIBTBM_LIBS $UDEV_LIBS $XDBG_LIBS $PNG_LIBS"

# check modules needed for build functioanl tests
if test "x$FT" != "xno"; then

    # xv functional tests
    PKG_CHECK_MODULES(XV_TEST, [xcb xcb-atom xcb-shm xcb-xv xcb-util xcb-dri2],
		      [xv_test_modules=yes], [xv_test_modules=no])
    
    if test "x$xv_test_modules" != "xyes"; then
        AC_MSG_ERROR([Not found modules for xv functional tests.])
    fi
    
    XV_TEST_CFLAGS="$XV_TEST_CFLAGS $EXYNOS_CFLAGS"
    XV_TEST_LIBS="$XV_TEST_LIBS $EXYNOS_LIBS"
 
    # hwc functional tests
    PKG_CHECK_MODULES(HWC_TEST, [xcb xcb-hwc xcb-composite xcb-damage xcb-dri2 xcb-dri3 xcb-present dlog],
		      [hwc_test_modules=yes], [hwc_test_modules=no])
    
    if test "x$hwc_test_modules" != "xyes"; then
        AC_MSG_ERROR([Not found modules for hwc functional tests.])
    fi

    HWC_TEST_CFLAGS="$HWC_TEST_CFLAGS $EXYNOS_CFLAGS"
    HWC_TEST_LIBS="$HWC_TEST_LIBS $EXYNOS_LIBS"
    
    # hwa functional tests
    PKG_CHECK_MODULES(HWA_TEST, [xcb xcb-hwa xcb-randr],
		      [hwa_test_modules=yes], [hwa_test_modules=no])
    
    if test "x$hwa_test_modules" != "xyes"; then
        AC_MSG_ERROR([Not found modules for hwa functional tests.])
    fi
    
    HWA_TEST_CFLAGS="$HWA_TEST_CFLAGS $EXYNOS_CFLAGS"
    HWA_TEST_LIBS="$HWA_TEST_LIBS $EXYNOS_LIBS"

    # pixmap copy test
    PKG_CHECK_MODULES(PIXMAP_COPY_TEST, [x11],
		      [pixmap_copy_test_modules=yes], [pixmap_copy_test_modules=no])

    if test "x$pixmap_copy_test_modules" != "xyes"; then
        AC_MSG_ERROR([Not found modules for pixmap_copy functional tests.])
    fi

    PIXMAP_COPY_TEST_CFLAGS="$PIXMAP_COPY_TEST_CFLAGS $EXYNOS_CFLAGS"
    PIXMAP_COPY_TEST_LIBS="$PIXMAP_COPY_TEST_LIBS $EXYNOS_CFLAGS"
    
    ftests=yes
fi

AM_CONDITIONAL(HAVE_FT, test "x$ftests" = "xyes")

AM_CONDITIONAL(PCIACCESS, [test "x$PCIACCESS" = xyes])
if test "x$PCIACCESS" = xyes; then
    AC_DEFINE(PCIACCESS, 1, [Use libpciaccess])
    PKG_CHECK_MODULES([PCIACCESS], [pciaccess >= 0.8.0])
    XORG_CFLAGS="$XORG_CFLAGS $PCIACCESS_CFLAGS "
fi

AM_CONDITIONAL(LEGACY_INTERFACE, [test "x$LEGACY_INTERFACE" = xyes])
if test "x$LEGACY_INTERFACE" = xyes; then
    AC_DEFINE(LEGACY_INTERFACE, 1, [Enable using of legacy kernel interface])
fi

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC

if test "x$DRI3" = xyes; then
    AC_CHECK_FILES( /usr/include/xorg/dri3.h /usr/include/xorg/present.h, [dri3=yes], [dri3=no]) 
    AM_CONDITIONAL(ENABLE_DRI3_PRESENT, [test "x$dri3" = xyes])
    if test "x$dri3" = xyes; then
        AC_DEFINE(HAVE_DRI3_PRESENT_H, 1, [ DRI3 and Present is enabled ])
    fi
else
    AM_CONDITIONAL(ENABLE_DRI3_PRESENT, [test "x$dri3" = xno])
fi

if test "x$HWC" = xyes; then
    AC_CHECK_FILES( /usr/include/xorg/hwc.h, [hwc=yes], [hwc=no]) 
    AM_CONDITIONAL(ENABLE_HWC, [test "x$hwc" = xyes])
    if test "x$hwc" = xyes; then
        AC_DEFINE(HAVE_HWC_H, 1, [ HWC is enabled ])
    else
        AC_MSG_ERROR([Not found hwc headers!])
    fi
else
    AM_CONDITIONAL(ENABLE_HWC, [test "x$hwc" = xno])
fi

if test "x$HWA" = xyes; then
    AC_CHECK_FILES( /usr/include/xorg/hwa.h, [hwa=yes], [hwa=no])
    AM_CONDITIONAL(ENABLE_HWA, [test "x$hwa" = xyes])
    if test "x$hwa" = xyes; then
        AC_DEFINE(HAVE_HWA_H, 1, [ HWA is enabled ])
    else
        AC_MSG_ERROR([Not found hwa headers!])
    fi
else
    AM_CONDITIONAL(ENABLE_HWA, [test "x$hwa" = xno])
fi

AC_SUBST([EXYNOS_CFLAGS])
AC_SUBST([EXYNOS_LIBS])

AC_SUBST([XORG_CFLAGS])
AC_SUBST([moduledir])

DRIVER_NAME=exynos
AC_SUBST([DRIVER_NAME])

XORG_MANPAGE_SECTIONS
XORG_RELEASE_VERSION

AC_CONFIG_FILES([
	Makefile
	src/Makefile
	man/Makefile
    tests/functional/Makefile
    tests/functional/xv_test/Makefile
    tests/functional/hwc_test/Makefile
    tests/functional/hwa_test/Makefile
    tests/functional/pixmap_copy_test/Makefile
])
AC_OUTPUT
