if HAVE_FT
bin_PROGRAMS = test_xv
test_xv_CFLAGS = @XV_TEST_CFLAGS@ \
-I@top_srcdir@/tests/functional/xv_test \
-I@top_srcdir@/src/xv
test_xv_LDADD = @XV_TEST_LIBS@
test_xv_SOURCES = \
@top_srcdir@/tests/functional/xv_test/xv_test.c \
@top_srcdir@/tests/functional/xv_test/data.c \
@top_srcdir@/tests/functional/xv_test/xcb_api.c \
@top_srcdir@/tests/functional/xv_test/test_xv_resize.c
endif
