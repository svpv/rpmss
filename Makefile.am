AM_CFLAGS = -Wall -Wextra -D_GNU_SOURCE

lib_LTLIBRARIES = librpmss.la
librpmss_la_SOURCES = rpmss.c rpmset.c rpmsetcmp.c

bin_PROGRAMS = mkset setcmp test-rpmss
mkset_SOURCES = mkset.c
mkset_LDADD = librpmss.la

setcmp_SOURCES = setcmp.c
setcmp_LDADD = librpmss.la

test_rpmss_SOURCES = test-rpmss.c
test_rpmss_LDADD = librpmss.la
