# Add the m4 directory to the include path for aclocal
ACLOCAL_AMFLAGS=-I m4

# Set up global variables
LIBNAME = upthread
CAP_LIBNAME = $(shell echo $(LIBNAME) | sed -e 's/^[a-z]/\U&/g')
SRCDIR = $(srcdir)/@SRCDIR@

# List of C FILES to build into objects
LIB_CFILES = \
  @SRCDIR@/upthread.c

LIB_HFILES = \
  @SRCDIR@/upthread.h

LIB_FILES = $(LIB_CFILES) $(LIB_HFILES)

# Setup parameters to build the library
lib_LTLIBRARIES = libupthread.la
libupthread_la_CFLAGS = $(AM_CFLAGS)
libupthread_la_CFLAGS += -I$(SYSDEPDIR)
libupthread_la_SOURCES = $(LIB_FILES)
if STATIC_ONLY
libupthread_la_LIBADD = $(LPARLIB) -lpthread
libupthread_la_LDFLAGS = -all-static
else
libupthread_la_LIBADD = $(LPARLIB)
endif

# Setup a directory where all of the include files will be installed
upthreadincdir = $(includedir)/upthread
dist_upthreadinc_DATA = $(LIB_HFILES)

# Setup parameters to build the test programs
check_PROGRAMS = upthread_test

upthread_test_SOURCES =  @TESTSDIR@/upthread_test.c
upthread_test_CFLAGS = $(AM_CFLAGS)
upthread_test_CFLAGS += -I$(SRCDIR) -I$(SYSDEPDIR)
if STATIC_ONLY
upthread_test_LDADD = -lupthread $(LPARLIB) -lpthread
upthread_test_LDFLAGS = -all-static
else
upthread_test_LDADD = -lupthread $(LPARLIB)
endif

ChangeLog:
	cd $(srcdir); \
	if [ -f git-changelog.sh ]; then \
	  ./git-changelog.sh; \
	fi;

uninstall-hook:
	rm -rf $(upthreadincdir)

.PHONY: ChangeLog

