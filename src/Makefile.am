AM_CFLAGS = -I@top_srcdir@/src @XORG_CFLAGS@ @EXYNOS_CFLAGS@ @TTRACE_CFLAGS@
exynos_drv_la_LTLIBRARIES = exynos_drv.la
#exynos_drv_la_LDFLAGS = -module -avoid-version @EXYNOS_LIBS@
exynos_drv_la_LDFLAGS = -module -avoid-version
exynos_drv_la_LIBADD = @EXYNOS_LIBS@ @TTRACE_LIBS@
exynos_drv_ladir = @moduledir@/drivers

# common
exynos_drv_la_SOURCES = \
	exynos.c

# crtcconfig
exynos_drv_la_SOURCES += \
	crtcconfig/exynos_crtc.c \
	crtcconfig/exynos_output.c \
	crtcconfig/exynos_plane.c \
	crtcconfig/exynos_layer.c \
	crtcconfig/exynos_prop.c \
	crtcconfig/exynos_xberc.c \
	crtcconfig/exynos_display.c \
	crtcconfig/exynos_dummy.c \
	crtcconfig/exynos_layer_manager.c
AM_CFLAGS += -I@top_srcdir@/src/crtcconfig

# accel
exynos_drv_la_SOURCES += \
	accel/exynos_exa.c \
	accel/exynos_exa_sw.c \
	accel/exynos_dri2.c
AM_CFLAGS += -I@top_srcdir@/src/accel

if ENABLE_DRI3_PRESENT
exynos_drv_la_SOURCES += \
	accel/exynos_present.c \
	accel/exynos_dri3.c
endif

# xv
exynos_drv_la_SOURCES += \
	xv/exynos_video_tvout.c \
	xv/exynos_video_virtual.c \
	xv/exynos_video_display.c \
	xv/exynos_video_clone.c \
	xv/exynos_video.c
AM_CFLAGS += -I@top_srcdir@/src/xv

# util
exynos_drv_la_SOURCES += \
	util/exynos_util.c
AM_CFLAGS += -I@top_srcdir@/src/util

# debug
exynos_drv_la_SOURCES += \
	debug/exynos_drmmode_dump.c
AM_CFLAGS += -I@top_srcdir@/src/debug

# memory flush
#exynos_drv_la_SOURCES += \
#	memory/exynos_memory_flush.c
#AM_CFLAGS += -I@top_srcdir@/src/memory

# neon
exynos_drv_la_SOURCES += \
	neon/memcpy_neon.s \
	neon/copy_area.c
AM_CFLAGS += -I@top_srcdir@/src/neon

# ipp
exynos_drv_la_SOURCES += \
	ipp/exynos_drm_ipp.c \
	ipp/exynos_converter.c \
	ipp/exynos_wb.c
AM_CFLAGS += -I@top_srcdir@/src/ipp

# g2d
exynos_drv_la_SOURCES += \
        g2d/fimg2d.c \
        g2d/util_g2d.c
AM_CFLAGS += -I@top_srcdir@/src/g2d

# hwc
if ENABLE_HWC
exynos_drv_la_SOURCES += \
        hwc/exynos_hwc.c
endif         
AM_CFLAGS += -I@top_srcdir@/src/hwc

# hwa
if ENABLE_HWA
exynos_drv_la_SOURCES += \
        hwa/exynos_hwa.c
endif
AM_CFLAGS += -I@top_srcdir@/src/hwa

