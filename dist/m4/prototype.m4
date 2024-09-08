# Define NEED_$1_PROTOTYPE if $1 cannot be found with just the includes in $2.
AC_DEFUN([VI_PROTOTYPE], [
    VI_PROTOTYPE3($1, m4_toupper($1), $2)
])

AC_DEFUN([VI_PROTOTYPE3], [

AC_SUBST(NEED_$2_PROTO)
AC_CACHE_CHECK([whether $1 prototype needed], vi_cv_proto_$1,
[AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[$3
typedef int     (*funcPtr)();
]], [[funcPtr ptr = (funcPtr) $1;]])],
vi_cv_proto_$1=no,
vi_cv_proto_$1=yes)])
if test "$vi_cv_proto_$1" = yes; then
	AC_DEFINE(NEED_$2_PROTO, 1,
		[Define when $1 prototype not in an obvious place.])
fi

])
