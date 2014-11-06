# Add the m4 directory to the include path for aclocal
ACLOCAL_AMFLAGS=-I m4

# Set up global variables
LIBNAME = upthread-pvcq
CAP_LIBNAME = $(shell echo $(LIBNAME) | sed -e 's/^[a-z]/\U&/g')
SRCDIR = $(srcdir)/@SRCDIR@

# List of C FILES to build into objects
LIB_CFILES = \
  @SRCDIR@/upthread.c \
  @SRCDIR@/mutex.c \
  @SRCDIR@/condvar.c \
  @SRCDIR@/barrier.c \
  @SRCDIR@/semaphore.c \
  @SRCDIR@/futex.c

LIB_HFILES = \
  @SRCDIR@/upthread.h \
  @SRCDIR@/semaphore.h \
  @SRCDIR@/futex.h

LIB_FILES = $(LIB_CFILES) $(LIB_HFILES)

# Setup parameters to build the library
lib_LTLIBRARIES = libupthread-pvcq.la
libupthread_pvcq_la_CFLAGS = $(AM_CFLAGS)
libupthread_pvcq_la_CFLAGS += -I$(SYSDEPDIR)
libupthread_pvcq_la_SOURCES = $(LIB_FILES)
libupthread_pvcq_la_LIBADD = $(LPARLIB)

# Setup a directory where all of the include files will be installed
upthread_pvcqincdir = $(includedir)/upthread-pvcq
dist_upthread_pvcqinc_DATA = $(LIB_HFILES)

# Setup parameters to build the test programs
check_PROGRAMS = upthread_test syscall_test

upthread_test_SOURCES =  @TESTSDIR@/upthread_test.c
upthread_test_CFLAGS = $(AM_CFLAGS)
upthread_test_CFLAGS += -I$(SRCDIR) -I$(SYSDEPDIR)
upthread_test_LDADD = -lupthread-pvcq $(LPARLIB)

syscall_test_SOURCES =  @TESTSDIR@/syscall_test.c
syscall_test_CFLAGS = $(AM_CFLAGS)
syscall_test_CFLAGS += -I$(SRCDIR) -I$(SYSDEPDIR)
syscall_test_LDADD = -lupthread-pvcq $(LPARLIB)

ChangeLog:
	cd $(srcdir); \
	if [ -f git-changelog.sh ]; then \
	  ./git-changelog.sh; \
	fi;

uninstall-hook:
	rm -rf $(upthread_pvcqincdir)

.PHONY: ChangeLog

