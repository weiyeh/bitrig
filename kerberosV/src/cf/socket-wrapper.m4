dnl $Id: socket-wrapper.m4,v 1.1 2013/06/17 18:57:41 robert Exp $
dnl
AC_DEFUN([samba_SOCKET_WRAPPER], [

AC_ARG_ENABLE(socket-wrapper,
	AS_HELP_STRING([--enable-socket-wrapper],
		[use sambas socket-wrapper for testing]))

AM_CONDITIONAL(have_socket_wrapper, test "x$enable_socket_wrapper" = xyes)dnl

if test "x$enable_socket_wrapper" = xyes ; then
       AC_DEFINE(SOCKET_WRAPPER_REPLACE, 1,
               [Define if you want to use samba socket wrappers.])
fi

])
