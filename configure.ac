AC_INIT([pbuilder], [0.1], [https://paguilar.org], [pbuilder], [https://paguilar.org])
AC_CONFIG_SRCDIR([src])
AM_INIT_AUTOMAKE([-Wall])

PKG_CHECK_MODULES(glib2, [glib-2.0 >= 2.32.0])

dnl AC_CHECK_LIB([json-c], [json_object_new_object])

PBUILDER_CFLAGS="-Wall $glib2_CFLAGS"
PBUILDER_LIBS="$glib2_LIBS -lpthread"

AC_SUBST(PBUILDER_LIBS)
AC_SUBST(PBUILDER_CFLAGS) 

AC_PROG_CC

AC_OUTPUT(Makefile src/Makefile)
