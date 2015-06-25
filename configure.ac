m4_include([version.m4]) # fetch version number
m4_include([compat.m4]) # pkg config compatibility defines

AC_PREREQ([2.69])
AC_INIT([ccontrol], [VERSION_NUMBER], [ccontrol-comits@ligforge.imag.fr])

AC_CONFIG_SRCDIR([src/common/ccontrol_types.h]) # are we in the right source dir ?
AC_CONFIG_AUX_DIR([build-aux]) # build tools in separate dir
AC_CONFIG_MACRO_DIR([m4]) # macro in srcdir/m4

# automake should fail on any error
AM_INIT_AUTOMAKE([-Wall -Werror foreign subdir-objects])

# checks
AC_LANG_C
AC_PROG_CC
AC_PROG_CC_STDC
AC_PROG_MAKE_SET
AC_PROG_MKDIR_P
AC_PROG_LN_S
AM_PROG_AR
AC_PROG_LIBTOOL
PKG_PROG_PKG_CONFIG

AC_CHECK_HEADERS([fcntl.h stdlib.h string.h sys/ioctl.h unistd.h])
AC_TYPE_SIZE_T
AC_FUNC_ERROR_AT_LINE
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_CHECK_FUNCS([munmap strchr strtoul])

# Pkg config install path
PKG_INSTALLDIR

# Udev rules path
PKG_CHECK_VAR([UDEVDIR], [udev], [udevdir], , [AC_MSG_ERROR([Cannot get required UDEVDIR variable from pkgconfig])])
AC_SUBST([udevrulesdir], [${UDEVDIR}/rules.d])

# Locate kernel build files
AC_ARG_VAR([KERNEL_SRC_PATH], [Kernel build files path (overrides current kernel version)])
AS_VAR_SET_IF([KERNEL_SRC_PATH], , [AS_VAR_SET([KERNEL_SRC_PATH], [/lib/modules/`uname -r`/build])]) # default
AC_CHECK_FILE([${KERNEL_SRC_PATH}/Makefile], , [AC_MSG_ERROR([Cannot find kernel module build Makefile])])

# Prepate files and create configure
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile
					  src/Makefile
					  src/common/Makefile
					  src/lib/Makefile
					  src/utils/Makefile
					  ccontrol.pc])
AC_OUTPUT
