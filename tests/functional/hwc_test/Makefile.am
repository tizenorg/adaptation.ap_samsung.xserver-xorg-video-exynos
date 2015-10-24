if HAVE_FT

 lib_LTLIBRARIES = libdri2_dri3.la
 libdri2_dri3_la_CFLAGS  = $(HWC_TEST_CFLAGS) 
 libdri2_dri3_la_LIBADD  = $(HWC_TEST_LIBS) -lpthread
 libdri2_dri3_la_LDFLAGS = -avoid-version
 libdri2_dri3_la_SOURCES =  aux_dri2_dri3_lib/dri2_dri3.c \
			    aux_dri2_dri3_lib/dri2.c \
			    aux_dri2_dri3_lib/dri3.c \
			    aux_dri2_dri3_lib/fps.c \
			    aux_dri2_dri3_lib/aux_draw.c   

 bin_PROGRAMS = hwc-sample square-bubbles clock snowflake wander-stripe

 hwc_sample_CFLAGS  = $(HWC_TEST_CFLAGS)
 hwc_sample_LDADD   = $(HWC_TEST_LIBS) -lpthread
 hwc_sample_SOURCES = 	./hwc_sample/hwc-sample.c \
			./hwc_sample/hwc-thread.c

 square_bubbles_CFLAGS  = $(HWC_TEST_CFLAGS)
 square_bubbles_LDADD   = $(HWC_TEST_LIBS) -lm -lX11
 square_bubbles_SOURCES = ./square_bubbles/square_bubbles.c 
 
 clock_CFLAGS  = $(HWC_TEST_CFLAGS) -I@top_srcdir@/tests/functional/hwc_test/aux_dri2_dri3_lib
 clock_LDADD   = $(HWC_TEST_LIBS) libdri2_dri3.la -lm
 clock_SOURCES = ./clock.c \
		 ./aux_apps.c
 
 snowflake_CFLAGS  = $(HWC_TEST_CFLAGS) -I@top_srcdir@/tests/functional/hwc_test/aux_dri2_dri3_lib
 snowflake_LDADD   = $(HWC_TEST_LIBS) libdri2_dri3.la -lm
 snowflake_SOURCES = ./snowflake.c \
		     ./aux_apps.c
 
 wander_stripe_CFLAGS  = $(HWC_TEST_CFLAGS) -I@top_srcdir@/tests/functional/hwc_test/aux_dri2_dri3_lib
 wander_stripe_LDADD   = $(HWC_TEST_LIBS) libdri2_dri3.la
 wander_stripe_SOURCES = ./wander_stripe.c \
			 ./aux_apps.c
 
# install-data-hook:
 dist_data_DATA = launch.sh 
endif
