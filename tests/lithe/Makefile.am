# I need this conditional here so that I only build tests for the variant of
# interest. See the top level Makefile.am, and notice how I have to manually
# include the tests for all variants.  This conditional then filters out the
# ones I don't want.
if LITHE_VARIANT

# Setup parameters to build the test programs
check_PROGRAMS = upthread_test syscall_test

upthread_test_SOURCES =  @TESTSDIR@/upthread_test.c
upthread_test_CFLAGS = $(AM_CFLAGS)
upthread_test_CFLAGS += $(LIB_INCLUDE_PATH)
upthread_test_LDADD = libupthread@VARIANT_SUFFIX@.la -lithe -lparlib

syscall_test_SOURCES =  @TESTSDIR@/syscall_test.c
syscall_test_CFLAGS = $(AM_CFLAGS)
syscall_test_CFLAGS += $(LIB_INCLUDE_PATH)
syscall_test_LDADD = libupthread@VARIANT_SUFFIX@.la -lithe -lparlib

endif
