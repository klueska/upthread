# Add the m4 directory to the include path for aclocal
ACLOCAL_AMFLAGS=-I m4

# Set up global variables
LIBNAME = upthread-juggle
CAP_LIBNAME = $(shell echo $(LIBNAME) | sed -e 's/^[a-z]/\U&/g')
SRCDIR = $(srcdir)/@SRCDIR@

# List of C FILES to build into objects
LIB_CFILES = \
  @SRCDIR@/upthread.c

LIB_HFILES = \
  @SRCDIR@/upthread.h

LIB_FILES = $(LIB_CFILES) $(LIB_HFILES)

# Setup parameters to build the library
lib_LTLIBRARIES = libupthread-juggle.la
libupthread_juggle_la_CFLAGS = $(AM_CFLAGS)
libupthread_juggle_la_CFLAGS += -I$(SYSDEPDIR)
libupthread_juggle_la_SOURCES = $(LIB_FILES)
if STATIC_ONLY
libupthread_juggle_la_LIBADD = $(LPARLIB) -lpthread
libupthread_juggle_la_LDFLAGS = -all-static
else
libupthread_juggle_la_LIBADD = $(LPARLIB)
endif

# Setup a directory where all of the include files will be installed
upthreadincdir = $(includedir)/upthread-juggle
dist_upthreadinc_DATA = $(LIB_HFILES)

# Setup parameters to build the test programs
check_PROGRAMS = preempt_test

preempt_test_SOURCES =  @TESTSDIR@/preempt_test.c
preempt_test_CFLAGS = $(AM_CFLAGS)
preempt_test_CFLAGS += -I$(SRCDIR) -I$(SYSDEPDIR)
if STATIC_ONLY
preempt_test_LDADD = -lupthread-juggle $(LPARLIB) -lpthread
preempt_test_LDFLAGS = -all-static
else
preempt_test_LDADD = -lupthread-juggle $(LPARLIB)
endif

ChangeLog:
	cd $(srcdir); \
	if [ -f git-changelog.sh ]; then \
	  ./git-changelog.sh; \
	fi;

uninstall-hook:
	rm -rf $(upthreadincdir)

.PHONY: ChangeLog

