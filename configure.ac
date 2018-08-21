AC_INIT([spettro],[0.1])
AM_INIT_AUTOMAKE
AC_PROG_CC
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

AC_CHECK_HEADERS([errno.h])
AC_CHECK_HEADERS([fftw3.h])
AC_CHECK_HEADERS([limits.h])
AC_CHECK_HEADERS([malloc.h])
AC_CHECK_HEADERS([math.h])
AC_CHECK_HEADERS([stdio.h])
AC_CHECK_HEADERS([stdlib.h])
AC_CHECK_HEADERS([string.h])

AC_CHECK_LIB([fftw3],[lock_fftw3])
AC_CHECK_LIB([m],[lrint])
