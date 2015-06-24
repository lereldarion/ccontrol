m4_include([version.m4]) # fetch version number

AC_PREREQ([2.69])
AC_INIT([ccontrol],[VERSION_NUMBER],[ccontrol-comits@ligforge.imag.fr])

AC_CONFIG_SRCDIR([src/common/ccontrol_types.h]) # are we in the right source dir ?
AC_CONFIG_AUX_DIR([build-aux]) # build tools in separate dir
AC_CONFIG_MACRO_DIR([m4]) # macro in srcdir/m4

# automake should fail on any error
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# checks
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_MAKE_SET
AM_PROG_AR
AC_PROG_LIBTOOL

AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h unistd.h])
AC_TYPE_SIZE_T
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([munmap strchr strtoul])

# TODO check pkgconfig state and handle old versions (up to 0.26 i think)

# Pkg config path
PKG_PROG_PKG_CONFIG([0.28])
PKG_INSTALLDIR

# Udev rules path
PKG_CHECK_VAR([UDEVDIR],[udev],[udevdir],[],[AC_MSG_ERROR([Cannot retrieve udevdir pkgconfig variable])])
AC_SUBST([udevrulesdir],[${UDEVDIR}/rules.d])

# Prepate files and create configure
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile
					  src/Makefile
					  src/common/Makefile
					  src/lib/Makefile
					  src/module/Makefile
					  src/utils/Makefile
					  ccontrol.pc])
AC_OUTPUT
