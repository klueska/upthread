# Require a minimum autoconf version
AC_PREREQ([2.65])

# Initialize autoconf
# Specify package's name, version number, and bug-report address.
AC_INIT([upthread], [1.0], [klueska@cs.berkeley.edu])

# Directory containing any external m4 macros
AC_CONFIG_MACRO_DIR([m4])

# Auxiliary scripts such as install-sh and depcomp should be in DIRECTORY
AC_CONFIG_AUX_DIR([build-aux])

# Make the default prefix /usr instead of /usr/local
AC_PREFIX_DEFAULT([/usr])

# Initialize automake
# Turn on all Automake warnings and report them as errors.
# This is a foreign package
AM_INIT_AUTOMAKE([subdir-objects foreign -Wall -Werror -Wno-portability])

# Checks for a C compiler
AC_PROG_CC

# Checks for a C++ compiler
#AC_PROG_CXX

# Check for an assembler
AM_PROG_AS

# Checks for libtool
AC_PROG_LIBTOOL

# Check for HEADERS and #define HAVE_HEADER_H for each header found
#AC_CHECK_HEADERS([HEADERS ...])

# Output the following to config.h
#AC_DEFINE(VARIABLE, VALUE, DESCRIPTION)

# Declare config.h as the output header
AC_CONFIG_HEADERS([config.h])

# Declare Makefiles as output files
AC_CONFIG_FILES([
  Makefile
])

# A safety check. FILE is a distributed source file, making sure that
# 'configure' is not run from outerspace.
AC_CONFIG_SRCDIR([src/common/mutex.c])

# Set the common AM_CFLAGS for all Makefile.am files
MY_CFLAGS="                \
  -std=gnu99               \
  -g -O2 -Wall             \
  -Wno-unused-function     \
  -Wno-unused-value        \
  -Wno-missing-braces      \
  -fno-exceptions          \
  -fno-strict-aliasing     \
"

# Set the common AM_CFLAGS for all Makefile.am files
AC_SUBST([AM_CFLAGS],["$MY_CFLAGS"])

# Set up some global variables for use in the makefile
SRCDIR=src
COMMONDIR=$SRCDIR/common
AC_SUBST([SRCDIR])
AC_SUBST([COMMONDIR])
AC_SUBST([LPARLIB])
AM_SUBST_NOTMAKE([SRCDIR])
AM_SUBST_NOTMAKE([COMMONDIR])

# Add parlib -L and -I directories into LDFLAGS and C/CXXFLAGS respectively,
# if they are not install on the system already
AC_ARG_WITH([parlib],
  [AS_HELP_STRING([--with-parlib],
    [path to your parlib installation if not in a standard location])],
  [PARLIBPATH="$withval"], [])

CFLAGS="$CFLAGS -I$PARLIBPATH/include"
CXXFLAGS="$CXXFLAGS -I$PARLIBPATH/include"
LDFLAGS="$LDFLAGS -L$PARLIBPATH/lib"

# Specify the variant we want to build
AC_ARG_WITH([variant],
  [AS_HELP_STRING([--with-variant],
    [name of the upthread variant you would like to build for.
     Current variants include 'gq (global queue)', 'pvcq (per-vcore queue)',
     and 'juggle (preemptive per-vcore queue)'.]
    )],
  [VARIANT="$withval"
   VARIANTDIR="$SRCDIR/$withval"
   TESTSDIR="tests/$withval"], [
  echo "Error! You must specify a upthread variant to build for."
  echo "Variants include 'gq', 'pvcq', and 'juggle'"
  echo "See 'configure --help' for details."
  exit -1
])
AC_SUBST([VARIANTDIR])
AC_SUBST([TESTSDIR])

# Define conditionals for each of the valid variants that we have
AM_CONDITIONAL([GQ_VARIANT], test "x$VARIANT" = xgq)
AM_CONDITIONAL([JUGGLE_VARIANT], test "x$VARIANT" = xjuggle)
AM_CONDITIONAL([PVCQ_VARIANT], test "x$VARIANT" = xpvcq)

# Add a suffix to the name of the library for variant being built
AC_ARG_WITH([suffix],
  [AS_HELP_STRING([--with-suffix],
	[suffix to append to the include directory and libname for the upthread
	 variant you are building for (i.e. upthread-SUFFIX). By default, NO suffix
	 is appended, and any previously installed upthread library will be
     overwritten.])],
  [VARIANT_SUFFIX="-$withval"],
  [VARIANT_SUFFIX=])
AC_SUBST([VARIANT_SUFFIX])

# Check whether LIBRARY exists and contains FUNCT
# Execute ACT-IF-FOUND if it does.  ACT-IF-NOT otherwise.
#AC_CHECK_LIB(LIBRARY, FUNCT, [ACT-IF-FOUND], [ACT-IF-NOT])
if [[ "$host_os" != "ros" ]]; then
save_LDFLAGS="$LDFLAGS"
save_LIBS="$LIBS"
LDFLAGS="$LDFLAGS -static -lpthread"
LIBS="$LIBS -lpthread"
AC_CHECK_LIB(parlib, uthread_init, [], [
    echo "Error! You need to have parlib installed on your system in order to build upthread!"
    echo "Try setting --with-parlib=<parlib_install_dir>"
    echo "See 'configure --help' for details."
    exit -1
])
LDFLAGS="$save_LDFLAGS"
LIBS="$save_LIBS"
LPARLIB=-lparlib
else
LPARLIB=
fi

# Actually output all declared files
AC_OUTPUT
