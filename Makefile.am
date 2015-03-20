# Add the m4 directory to the include path for aclocal
ACLOCAL_AMFLAGS=-I m4

# Set up global variables
LIBNAME = upthread@VARIANT_SUFFIX@
CAP_LIBNAME = $(shell echo $(LIBNAME) | sed -e 's/^[a-z]/\U&/g')
SRCDIR = $(srcdir)/@SRCDIR@
VARIANTDIR = $(srcdir)/@VARIANTDIR@
COMMONDIR = $(srcdir)/@COMMONDIR@

# List of C FILES to build into objects
if LITHE_VARIANT
LIB_CFILES = \
  @VARIANTDIR@/mutex.c \
  @VARIANTDIR@/condvar.c \
  @VARIANTDIR@/barrier.c \
  @VARIANTDIR@/semaphore.c \
  @COMMONDIR@/dtls.c \
  @COMMONDIR@/common.c \
  @COMMONDIR@/unsupported.c \
  @VARIANTDIR@/upthread.c
  #@VARIANTDIR@/futex.c \ #Included in lithe already
else
LIB_CFILES = \
  @COMMONDIR@/mutex.c \
  @COMMONDIR@/condvar.c \
  @COMMONDIR@/barrier.c \
  @COMMONDIR@/semaphore.c \
  @COMMONDIR@/futex.c \
  @COMMONDIR@/dtls.c \
  @COMMONDIR@/common.c \
  @COMMONDIR@/unsupported.c \
  @VARIANTDIR@/upthread.c
endif

if LITHE_VARIANT
LIB_HFILES = \
  @COMMONDIR@/semaphore.h \
  @VARIANTDIR@/futex.h \
  @VARIANTDIR@/upthread-types.h \
  @COMMONDIR@/upthread-common.h \
  @VARIANTDIR@/upthread.h
else
LIB_HFILES = \
  @COMMONDIR@/semaphore.h \
  @COMMONDIR@/futex.h \
  @COMMONDIR@/upthread-types.h \
  @COMMONDIR@/upthread-common.h \
  @VARIANTDIR@/upthread.h
endif

LIB_COMPAT_HFILES = \
  @COMMONDIR@/pthread.h

LIB_FILES = $(LIB_CFILES) $(LIB_HFILES)
LIB_INCLUDE_PATH = -I$(VARIANTDIR) -I$(COMMONDIR)

# Setup parameters to build the library
lib_LTLIBRARIES = libupthread@VARIANT_SUFFIX@.la
libupthread@VARIANT_SUFFIX@_la_CFLAGS = $(AM_CFLAGS)
libupthread@VARIANT_SUFFIX@_la_CFLAGS += $(LIB_INCLUDE_PATH)
libupthread@VARIANT_SUFFIX@_la_SOURCES = $(LIB_FILES)
libupthread@VARIANT_SUFFIX@_la_LIBADD = $(LLITHE) $(LPARLIB)

# Setup a directory where all of the include files will be installed
upthreadincdir = $(includedir)/upthread@VARIANT_SUFFIX@
upthreadcompatincdir = $(includedir)/upthread@VARIANT_SUFFIX@/compatibility
dist_upthreadinc_DATA = $(LIB_HFILES)
dist_upthreadcompatinc_DATA = $(LIB_COMPAT_HFILES)

# Setup parameters to build the test programs.
# Unfortunately, I have to list all of the variants here since @TESTSDIR@ wont
# be expanded until Makefile.in. In fact I can't use ANY variable substition
# in the below include directives, or they end up being silently passed
# through to Makefile.in, instead of #including the corresponding Makefile.am
# directly into this file.
include $(srcdir)/tests/gq/Makefile.am
include $(srcdir)/tests/juggle/Makefile.am
include $(srcdir)/tests/pvcq/Makefile.am
include $(srcdir)/tests/lithe/Makefile.am

ChangeLog:
	cd $(srcdir); \
	if [ -f git-changelog.sh ]; then \
	  ./git-changelog.sh; \
	fi;

uninstall-hook:
	rm -rf $(upthreadincdir)

.PHONY: ChangeLog

