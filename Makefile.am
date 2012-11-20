# Add the m4 directory to the include path for aclocal
ACLOCAL_AMFLAGS=-I m4

# Set up global variables
LIBNAME = rrthread-parlib
CAP_LIBNAME = $(shell echo $(LIBNAME) | sed -e 's/^[a-z]/\U&/g')
SRCDIR = $(srcdir)/@SRCDIR@

# List of C FILES to build into objects
LIB_CFILES = \
  @SRCDIR@/upthread.c

LIB_HFILES = \
  @SRCDIR@/upthread.h

LIB_FILES = $(LIB_CFILES) $(LIB_HFILES)

# Setup parameters to build the library
lib_LTLIBRARIES = librrthread-parlib.la
librrthread_parlib_la_CFLAGS = $(AM_CFLAGS)
librrthread_parlib_la_CFLAGS += -I$(SYSDEPDIR)
librrthread_parlib_la_SOURCES = $(LIB_FILES)
librrthread_parlib_la_LIBADD = -lparlib
if STATIC_ONLY 
librrthread_parlib_la_LDFLAGS = -all-static 
endif 

# Setup a directory where all of the include files will be installed
rrthread_parlibincdir = $(includedir)/parlib/rrthread
dist_rrthread_parlibinc_DATA = $(LIB_HFILES)

# Setup parameters to build the test programs
check_PROGRAMS = upthread_test

upthread_test_SOURCES =  @TESTSDIR@/upthread_test.c
upthread_test_CFLAGS = $(AM_CFLAGS)
upthread_test_CFLAGS += -I$(SRCDIR) -I$(SYSDEPDIR)
upthread_test_LDFLAGS = -all-static
upthread_test_LDADD = librrthread-parlib.la
if STATIC_ONLY 
upthread_test_LDADD += -lpthread
endif 

ChangeLog:
	cd $(srcdir); \
	if [ -f git-changelog.sh ]; then \
	  ./git-changelog.sh; \
	fi;

uninstall-hook:
	rm -rf $(rrthread_parlibincdir)

.PHONY: ChangeLog

