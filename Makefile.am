AM_CFLAGS = -Wall -Wextra -Werror -D_GNU_SOURCE

lib_LTLIBRARIES = librpmss.la librpmset.la librpmsetcmp.la
librpmss_la_SOURCES = rpmss.c
librpmss_la_CFLAGS = $(AM_CFLAGS) -Wno-override-init
librpmset_la_SOURCES = rpmset.c
librpmsetcmp_la_SOURCES = rpmsetcmp.c

bin_PROGRAMS = mkset setcmp test-rpmss setconv gen-kiely-k provided-symbols
mkset_SOURCES = mkset.c
mkset_LDADD = librpmset.la librpmss.la

setcmp_SOURCES = setcmp.c
setcmp_LDADD = librpmsetcmp.la librpmss.la

test_rpmss_SOURCES = test-rpmss.c
test_rpmss_LDADD = librpmss.la

setconv_SOURCES = setconv.c
setconv_LDADD = librpmss.la
setconv_CFLAGS = $(AM_CFLAGS) -Wno-override-init

gen_kiely_k_SOURCES = gen-kiely-k.c
gen_kiely_k_LDADD = -lm
gen_kiely_k_CFLAGS = $(AM_CFLAGS) -Ofast

provided_symbols_SOURCES = provided-symbols.c
provided_symbols_LDADD = -ldw
