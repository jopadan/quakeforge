dnl Process this file with autoconf to produce a configure script.
AC_PREREQ([2.69])

dnl This is the only place where the package name and version appear
AC_INIT([QuakeForge],m4_esyscmd(config.d/git-version-gen --prefix '' .tarball-version))
AM_INIT_AUTOMAKE([foreign subdir-objects])

dnl LT_INIT messes with CFLAGS (evil bastard)
if test "x${CFLAGS-unset}" = xunset; then
	CFLAGS=""
fi
saved_CFLAGS="$CFLAGS"
dnl LT_INIT([win32-dll])
LT_INIT
CFLAGS="$saved_CFLAGS"

AC_REVISION([$Revision$]) dnl

AC_CONFIG_HEADERS([include/config.h])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

m4_include(config.d/versions.m4)

AC_LANG([C])

if test "$x{AR-unset}" = xunset; then
	AR="ar"
fi
export AR
AC_SUBST(AR)
AC_SUBST(RANLIB)

m4_include(config.d/windows.m4)
m4_include(config.d/asm.m4)
m4_include(config.d/programs.m4)
m4_include(config.d/system_services.m4)
m4_include(config.d/header_files.m4)
m4_include(config.d/library_functions.m4)
m4_include(config.d/typedefs_structs_compiler.m4)

m4_include(config.d/cross_compile.m4)

m4_include(config.d/pthread.m4)

WINMM=""
case "$host_os" in
	cygwin*)
		WINMM="-lwinmm"
	;;
	mingw32*)
		WINMM="-lwinmm"
	;;
	*)
		dnl Checks for working -lm
		AC_MSG_CHECKING(for pow and atan2 in standard library)
		AC_LINK_IFELSE(
			[AC_LANG_PROGRAM(
				[[#include <math.h>
				int func_ind;
				double (*func[])(double, double) = {pow, atan2};]],
				[[return (int)(func[func_ind])(2, 3);]])],
			[AC_MSG_RESULT(yes)],
			[AC_MSG_RESULT(no)
			 AC_MSG_CHECKING(for pow and atan2 in -lm)
			 LIBS="-lm $LIBS"
			 AC_LINK_IFELSE(
				[AC_LANG_PROGRAM(
					[[#include <math.h>
					int func_ind;
					double (*func[])(double, double) = {pow, atan2};]],
					[[return (int)(func[func_ind])(2, 3);]])],
				[AC_MSG_RESULT(yes)],
				[AC_MSG_ERROR([math library (-lm) appears broken])]
			)]
		)
	;;
esac	

m4_include(config.d/compression.m4)

m4_include(config.d/vulkan.m4)
m4_include(config.d/x11.m4)

m4_include(config.d/curses.m4)
m4_include(config.d/freetype.m4)

m4_include(config.d/networking.m4)

m4_include(config.d/compiling.m4)
m4_include(config.d/attributes.m4)

dnl ==================================================================
dnl Checks for system type
dnl ==================================================================

dnl Checks for which system driver to use

AC_MSG_CHECKING(for system driver)
case "${host}" in
	i?86-*-mingw32*|x86_64-w64-mingw32*)
		SYSTYPE=WIN32
		AC_MSG_RESULT([Win32 driver])
		WIN32_LIBS=' $(NET_LIBS)'
		if test "x$host" != "x$build"; then
			case "$build_os" in
				cygwin*)
					WIN32_LIBS=" -L/usr/lib/w32api $WIN32_LIBS"
					;;
			esac
		fi

		;;
	i?86-*-cygwin*|x86_64-*-cygwin*)
		#SYSTYPE=WIN32
		AC_MSG_RESULT([Win32 driver])
		WIN32_LIBS=' $(NET_LIBS)'
		;;
	*-openbsd*)
		SYSTYPE=OPENBSD
		AC_MSG_RESULT([OpenBSD driver])
		WIN32_LIBS=''
		;;
	*)
		SYSTYPE=POSIX
		AC_MSG_RESULT([default POSIX driver])
		WIN32_LIBS=''
		;;
esac
AM_CONDITIONAL(SYSTYPE_WIN32, test "x$SYSTYPE" = "xWIN32")
AC_SUBST(WIN32_LIBS)

m4_include(config.d/sound.m4)
m4_include(config.d/joystick.m4)
m4_include(config.d/evdev.m4)
m4_include(config.d/cdrom.m4)

m4_include(config.d/paths.m4)

m4_include(config.d/build_control.m4)
m4_include(config.d/qfcc.m4)

m4_include(config.d/tracy.m4)
m4_include(config.d/backtrace.m4)

AC_ARG_ENABLE(static-doc,
	AS_HELP_STRING([--enable-static-doc],
		[enable generation of doxygen docs for static functions.]))
STATIC_DOC=NO
if test "x$enable_static_doc" = xyes; then
	STATIC_DOC=YES
fi
AC_SUBST(STATIC_DOC)

AC_ARG_ENABLE(typecheck-progs,
	AS_HELP_STRING([--enable-typecheck-progs],
		[enable type checking on progs field access]))
if test "x$enable_typecheck_progs" = xyes; then
	AC_DEFINE(TYPECHECK_PROGS, 1, [Define this if you want progs typechecking])
fi

TOPSRC=`readlink -f ${srcdir}`
AC_SUBST(TOPSRC)

case "${host}" in
	i?86-*-mingw32*|x86_64-w64-mingw32*)
		QF_REQUIRES([
			[libsamplerate, HAVE_SAMPLERATE],
			[flac, HAVE_FLAC],
			[ogg, HAVE_OGG],
			[vorbis, HAVE_VORBIS],
			[libpng, HAVE_PNG],
			[zlib, HAVE_ZLIB],
			[curl, HAVE_LIBCURL],
			[ncurses, HAVE_NCURSES],
			[vulkan-dev, HAVE_VULKAN],
			[glslang-tools, HAVE_GLSLANG],
			[fontconfig, HAVE_FONTCONFIG],
			[freetype, HAVE_FREETYPE],
			[harfbuzz, HAVE_HARFBUZZ],
		])
	;;
	*linux*)
		QF_REQUIRES([
			[libasound2-dev, HAVE_ALSA],
			[libjack-jackd2-dev, HAVE_JACK],
			[libsamplerate0-dev, HAVE_SAMPLERATE],
			[libflac-dev, HAVE_FLAC],
			[libogg-dev, HAVE_OGG],
			[libvorbis-dev, HAVE_VORBIS],
			[libpng-dev, HAVE_PNG],
			[zlib1g-dev, HAVE_ZLIB],
			[libcurl4-openssl-dev, HAVE_LIBCURL],
			[libncurses5-dev, HAVE_NCURSES],
			[libxext-dev, HAVE_X],
			[libxxf86dga-dev, HAVE_DGA],
			[libxxf86vm-dev, HAVE_VIDMODE],
			[libxi-dev, HAVE_XI2],
			[libxfixes-dev, HAVE_XFIXES],
			[libvulkan-dev, HAVE_VULKAN],
			[glslang-tools, HAVE_GLSLANG],
			[libfontconfig-dev, HAVE_FONTCONFIG],
			[libfreetype-dev, HAVE_FREETYPE],
			[libharfbuzz-dev, HAVE_HARFBUZZ],
		])
	;;
esac
dnl[vulkan-validationlayers, HAVE_],

dnl Output files
m4_include(config.d/rpm.m4)
AC_CONFIG_FILES(m4_include(config.d/ac_config_files.m4))
AC_OUTPUT

m4_include(config.d/result.m4)
