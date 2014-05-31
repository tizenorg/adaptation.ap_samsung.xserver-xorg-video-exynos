AM_CFLAGS = -I@top_srcdir@/src @XORG_CFLAGS@ @EXYNOS_CFLAGS@
exynos_drv_la_LTLIBRARIES = exynos_drv.la
#exynos_drv_la_LDFLAGS = -module -avoid-version @EXYNOS_LIBS@
exynos_drv_la_LDFLAGS = -module -avoid-version
exynos_drv_la_LIBADD = @EXYNOS_LIBS@
exynos_drv_ladir = @moduledir@/drivers

# common
exynos_drv_la_SOURCES = \
	sec.c

# crtcconfig
exynos_drv_la_SOURCES += \
	crtcconfig/sec_crtc.c \
	crtcconfig/sec_output.c \
	crtcconfig/sec_plane.c \
	crtcconfig/sec_layer.c \
	crtcconfig/sec_prop.c \
	crtcconfig/sec_xberc.c \
	crtcconfig/sec_display.c
AM_CFLAGS += -I@top_srcdir@/src/crtcconfig

# accel
exynos_drv_la_SOURCES += \
	accel/sec_exa.c \
	accel/sec_exa_sw.c \
	accel/sec_dri2.c
AM_CFLAGS += -I@top_srcdir@/src/accel

# xv
exynos_drv_la_SOURCES += \
	xv/sec_video_tvout.c \
	xv/sec_video_virtual.c \
	xv/sec_video_display.c \
	xv/sec_video.c
AM_CFLAGS += -I@top_srcdir@/src/xv

# util
exynos_drv_la_SOURCES += \
	util/sec_util.c 
AM_CFLAGS += -I@top_srcdir@/src/util

# debug
exynos_drv_la_SOURCES += \
	debug/sec_drmmode_dump.c
AM_CFLAGS += -I@top_srcdir@/src/debug

# memory flush
#exynos_drv_la_SOURCES += \
#	memory/sec_memory_flush.c
#AM_CFLAGS += -I@top_srcdir@/src/memory

# neon
exynos_drv_la_SOURCES += \
	neon/memcpy_neon.s \
	neon/copy_area.c
AM_CFLAGS += -I@top_srcdir@/src/neon

# ipp
exynos_drv_la_SOURCES += \
	ipp/sec_drm_ipp.c \
	ipp/sec_converter.c \
	ipp/sec_wb.c
AM_CFLAGS += -I@top_srcdir@/src/ipp

# g2d
exynos_drv_la_SOURCES += \
        accel/sec_exa_g2d.c \
        g2d/fimg2d.c \
        g2d/util_g2d.c
AM_CFLAGS += -I@top_srcdir@/src/g2d
