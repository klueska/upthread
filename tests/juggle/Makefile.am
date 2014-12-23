# I need this conditional here so that I only build tests for the variant of
# interest. See the top level Makefile.am, and notice how I have to manually
# include the tests for all variants.  This conditional then filters out the
# ones I don't want.
if JUGGLE_VARIANT

# Setup parameters to build the test programs
check_PROGRAMS = preempt_test 

preempt_test_SOURCES =  @TESTSDIR@/preempt_test.c
preempt_test_CFLAGS = $(AM_CFLAGS)
preempt_test_CFLAGS += $(LIB_INCLUDE_PATH)
preempt_test_LDADD = -lupthread@VARIANT_SUFFIX@ $(LPARLIB)

endif
