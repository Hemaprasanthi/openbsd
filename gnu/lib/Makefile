#	$OpenBSD: Makefile,v 1.23 2020/08/10 21:09:39 patrick Exp $

.include <bsd.own.mk>

SUBDIR+=libcompiler_rt libiberty libreadline
.if make(obj)
SUBDIR+=libstdc++ libstdc++-v3 libsupc++-v3
.else
.  if ${BUILD_GCC3:L} == "yes"
SUBDIR+=libstdc++
.  endif
.  if ${BUILD_GCC4:L} == "yes"
# XXX make sure we build libstdc++-v3 from gcc4
SUBDIR+=libstdc++-v3 libsupc++-v3
.  endif
.endif

.include <bsd.subdir.mk>
