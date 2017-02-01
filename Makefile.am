AM_CFLAGS = -Wall -Wextra -Werror -D_GNU_SOURCE

lib_LIBRARIES = librpmss.a librpmset.a librpmsetcmp.a
librpmss_a_SOURCES = rpmss.c
librpmss_a_CFLAGS = $(AM_CFLAGS) -Wno-override-init
librpmset_a_SOURCES = rpmset.c
librpmsetcmp_a_SOURCES = rpmsetcmp.c

bin_PROGRAMS = mkset setcmp test-rpmss setconv gen-kiely-k provided-symbols bench-lru bench-downsample
mkset_SOURCES = mkset.c
mkset_LDADD = librpmset.a librpmss.a

setcmp_SOURCES = setcmp.c
setcmp_LDADD = librpmsetcmp.a librpmss.a

test_rpmss_SOURCES = test-rpmss.c
test_rpmss_LDADD = librpmss.a

setconv_SOURCES = setconv.c
setconv_LDADD = librpmss.a
setconv_CFLAGS = $(AM_CFLAGS) -Wno-override-init

gen_kiely_k_SOURCES = gen-kiely-k.c
gen_kiely_k_LDADD = -lm
gen_kiely_k_CFLAGS = $(AM_CFLAGS) -Ofast

provided_symbols_SOURCES = provided-symbols.c
provided_symbols_LDADD = -ldw -lelf

bench_lru_SOURCES = bench.c bench-lru.c
bench_downsample_SOURCES = bench.c bench-downsample.c
bench_downsample_LDADD = librpmss.a

lib_LTLIBRARIES = dump-rpmsetcmp.la
dump_rpmsetcmp_la_LDFLAGS = -module -avoid-version

apt.setcmp.zst: dump-rpmsetcmp.la setconv
	apt-cache unmet &>/dev/null
	LD_PRELOAD=$$PWD/.libs/dump-rpmsetcmp.so \
	apt-cache unmet |grep ^rpmsetcmp |cut -f2,3 >$@.PR
	cut -f1 $@.PR |./setconv >$@.P
	cut -f2 $@.PR |./setconv >$@.R
	rm $@.PR
	paste $@.[PR] |zstd >$@
	rm $@.[PR]
