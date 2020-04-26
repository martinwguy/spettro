# configure.ac for spettro

AC_INIT([spettro],[0.1])
AC_CONFIG_HEADERS([configure.h])
AM_INIT_AUTOMAKE
AC_PROG_CC
AC_HEADER_STDC
AC_CONFIG_FILES([Makefile])
AC_OUTPUT

# Obligatory headers
AC_CHECK_HEADERS_ONCE([errno.h fftw3.h limits.h malloc.h math.h stdio.h])

# Optional headers
export CPPFLAGS="`pkg-config --cflags emotion`"
AC_CHECK_HEADER([Emotion.h])
export CPPFLAGS="`pkg-config --cflags evas`"
AC_CHECK_HEADER([Evas.h])
export CPPFLAGS="`pkg-config --cflags ecore-evas`"
AC_CHECK_HEADER([Ecore_Evas.h])
export CPPFLAGS="`pkg-config --cflags ecore`"
AC_CHECK_HEADER([Ecore.h])
export CPPFLAGS="`sdl-config --cflags`"
AC_CHECK_HEADER([SDL.h])
export CPPFLAGS=

AC_CHECK_HEADER([sndfile.h])
AC_CHECK_HEADER([audiofile.h])

# Obligatory libraries
AC_CHECK_LIB([m],[lrint])
AC_CHECK_LIB([fftw3],[fftw_plan_r2r_1d])

# Optional libraries
AC_CHECK_LIB([SDL],[SDL_Init])
AC_CHECK_LIB([emotion],[emotion_object_video_mute_set])
AC_CHECK_LIB([evas],[evas_object_image_add])
AC_CHECK_LIB([ecore],[ecore_main_loop_begin])
AC_CHECK_LIB([ecore_evas],[ecore_evas_init])
AC_CHECK_LIB([eo],[emotion_object_add])
AC_CHECK_LIB([sndfile],[sf_open])
AC_CHECK_LIB([audiofile],[afOpenFile])

# All done
AC_OUTPUT
