#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <dwarf.h>
#include <elfutils/libdwfl.h>

static int provided(GElf_Sym *sym, const char *name)
{
    return
	/* dl-lookup.c: /st_value */
	(sym->st_value != 0 || GELF_ST_TYPE(sym->st_info) == STT_TLS) &&
	/* dl-lookup.c: /st_shndx */
	(sym->st_shndx != SHN_UNDEF) &&
	/* dl-lookup.c: /ALLOWED_STT */
	(GELF_ST_TYPE(sym->st_info) == STT_NOTYPE ||
	 GELF_ST_TYPE(sym->st_info) == STT_OBJECT ||
	 GELF_ST_TYPE(sym->st_info) == STT_FUNC ||
	 GELF_ST_TYPE(sym->st_info) == STT_COMMON ||
	 GELF_ST_TYPE(sym->st_info) == STT_TLS ||
	 GELF_ST_TYPE(sym->st_info) == STT_GNU_IFUNC) &&
	/* dl-lookup.c: /Hidden and internal */
	(GELF_ST_VISIBILITY(sym->st_other) == STV_DEFAULT ||
	 GELF_ST_VISIBILITY(sym->st_other) == STV_PROTECTED) &&
	/* dl-lookup.c: /switch.*ST_BIND */
	(GELF_ST_BIND(sym->st_info) == STB_GLOBAL ||
	 GELF_ST_BIND(sym->st_info) == STB_WEAK ||
	 GELF_ST_BIND(sym->st_info) == STB_GNU_UNIQUE) &&
	/* Ignore special symbols found in each and every library. */
	(strcmp(name, "__bss_start") &&
	 strcmp(name, "_edata") &&
	 strcmp(name, "_end") &&
	 strcmp(name, "_fini") &&
	 strcmp(name, "_init"));
}

struct symx {
    GElf_Sym sym;
    const char *name;
    int done;
};

/* Sort provided symbols by address. */
static int provcmp(const void *x1, const void *x2)
{
    const struct symx *s1 = x1;
    const struct symx *s2 = x2;
    if (s1->sym.st_value > s2->sym.st_value)
	return 1;
    if (s1->sym.st_value < s2->sym.st_value)
	return -1;
    return 0;
}

static char *funcproto(Dwarf_Die *die)
{
    static char buf[128];
    char *p = buf;
    *p++ = '(';

    Dwarf_Die kid;
    if (dwarf_child(die, &kid) != 0)
	goto ret;
    do {
	if (dwarf_tag(&kid) != DW_TAG_formal_parameter)
	    continue;
	Dwarf_Attribute abuf;
	Dwarf_Attribute *attr = dwarf_attr(&kid, DW_AT_type, &abuf);
	if (attr == NULL)
	    return NULL;
	Dwarf_Die tbuf;
	Dwarf_Die *type = dwarf_formref_die(attr, &tbuf);
	if (type == NULL)
	    return NULL;
	if (dwarf_peel_type(type, type) != 0)
	    return NULL;
	if (p > buf + 1) {
	    *p++ = ',';
	    *p++ = ' ';
	}
	if (dwarf_tag(type) == DW_TAG_pointer_type)
	    *p++ = 'p';
	else
	    *p++ = 'i';
    }
    while (dwarf_siblingof(&kid, &kid) == 0);
ret:
    *p++ = ')';
    *p++ = '\0';
    return buf;
}

static void print_func_0(const char *name, int namelen,
			 const char *ver, const char *proto)
{
    printf("func %.*s%s%s%s%s", namelen, name,
	    ver ? ver : "",
	    proto ? "\t" : "\n",
	    proto ? proto : "",
	    proto ? "\n" : "");
}

/* For default symbol foo@@VER, also print foo without VER. */
static int compat_nover;

/* For foo(proto), also print foo without proto. */
static int compat_noproto;

static void print_func_1(const char *name, const char *proto)
{
    const char *ver = strchr(name, '@');
    if (ver && ver[1] == '@') {
	/* default version */
	print_func_0(name, ver - name, ver + 1, proto);
	if (compat_nover)
	    print_func_0(name, ver - name, NULL, proto);
    }
    else
	print_func_0(name, strlen(name), NULL, proto);
}

void print_func(struct symx *symx, Dwarf_Die *die)
{
    const char *proto = NULL;
    /* We do not use proto for mangled symbols. */
    if (!(symx->name[0] == '_' && symx->name[1] == 'Z'))
	proto = funcproto(die);
    print_func_1(symx->name, proto);
    if (proto && compat_noproto)
	print_func_1(symx->name, NULL);
}

#include <getopt.h>

static int verbose;

static const char *argv1(int argc, char **argv)
{
    enum {
	OPT_COMPAT_NOVER = 256,
	OPT_COMPAT_NOPROTO,
    };
    static const struct option longopts[] = {
	{ "verbose", 0, NULL, 'v' },
	{ "compat-nover", 0, NULL, OPT_COMPAT_NOVER },
	{ "compat-noproto", 0, NULL, OPT_COMPAT_NOPROTO },
    };
    int c;
    while ((c = getopt_long(argc, argv, "v", longopts, NULL)) != -1) {
	switch (c) {
	case 'v':
	    verbose = 1;
	    break;
	case OPT_COMPAT_NOVER:
	    compat_nover = 1;
	    break;
	case OPT_COMPAT_NOPROTO:
	    compat_noproto = 1;
	    break;
	default:
	    goto usage;
	}
    }
    argc -= optind;
    argv += optind;
    if (argc != 1) {
usage:	fputs("Usage: provided-symbols lib.so\n", stderr);
	exit(1);
    }
    return *argv;
}

int main(int argc, char **argv)
{
    const char *file = argv1(argc, argv);
    static const Dwfl_Callbacks cb = {
	.section_address = dwfl_offline_section_address,
	.find_debuginfo	 = dwfl_standard_find_debuginfo,
	.find_elf	 = dwfl_build_id_find_elf,
    };
    Dwfl *dwfl = dwfl_begin(&cb);
    assert(dwfl);
    Dwfl_Module *m = dwfl_report_offline(dwfl, file, file, -1);
    assert(m);
    GElf_Addr bias;
    Elf *elf = dwfl_module_getelf(m, &bias);
    assert(elf);
    Dwarf *dwarf = dwfl_module_getdwarf(m, &bias);
    assert(dwarf);

    /* Gather provided symbols. */
    int nsym = dwfl_module_getsymtab(m);
    int sym1 = dwfl_module_getsymtab_first_global(m);
    assert(nsym > 0);
    assert(sym1 >= 0 && sym1 < nsym);
    int nprov = 0;
    struct symx *prov = NULL;
    for (int i = sym1; i < nsym; i++) {
	GElf_Sym sym;
	const char *name = dwfl_module_getsym(m, i, &sym, NULL);
	if (!provided(&sym, name))
	    continue;
	int delta = 1024;
	if ((nprov & (delta - 1)) == 0)
	    prov = realloc(prov, sizeof(*prov) * (nprov + delta));
	prov[nprov++] = (struct symx) { sym, name, 0 };
    }
    assert(nprov > 0);
    qsort(prov, nprov, sizeof(*prov), provcmp);

    /* Iterate DIEs and match them to provided symbols. */
    setlinebuf(stdout);
    Dwarf_Die *cu = NULL;
    while ((cu = dwfl_module_nextcu(m, cu, &bias))) {
	Dwarf_Die kid;
	if (dwarf_child(cu, &kid) != 0)
	    continue;
	do {
	    int tag = dwarf_tag(&kid);
	    struct symx key;
	    if (tag == DW_TAG_subprogram) {
		Dwarf_Addr base, end;
		if (dwarf_ranges(&kid, 0, &base, &key.sym.st_value, &end) < 1)
		    continue;
	    }
	    else if (tag == DW_TAG_variable) {
		Dwarf_Attribute abuf;
		Dwarf_Attribute *attr = dwarf_attr(&kid, DW_AT_location, &abuf);
		if (attr == NULL)
		    continue;
		Dwarf_Op *expr;
		size_t elen;
		if (dwarf_getlocation(attr, &expr, &elen) != 0)
		    continue;
		if (elen == 1 && expr[0].atom == DW_OP_addr)
		    key.sym.st_value = expr[0].number;
		else
		    continue;
	    }
	    else
		continue;
	    key.sym.st_value += bias;
	    struct symx *symx = bsearch(&key, prov, nprov, sizeof(*prov), provcmp);
	    if (!symx) {
		Dwarf_Attribute abuf;
		Dwarf_Attribute *attr = dwarf_attr(&kid, DW_AT_name, &abuf);
		const char *name = dwarf_formstring(attr);
		if (name && verbose)
		    fprintf(stderr, "nothing for %s %s %lx\n",
				    tag == DW_TAG_subprogram ? "func" : "var",
				    name, key.sym.st_value);
		continue;
	    }
	    while (symx > prov && symx[-1].sym.st_value == key.sym.st_value)
		symx--;
	    do {
		if (tag == DW_TAG_subprogram)
		    print_func(symx, &kid);
		else
		    puts(symx->name);
		symx->done = 1;
		symx++;
	    }
	    while (symx < prov + nprov && symx->sym.st_value == key.sym.st_value);
	}
	while (dwarf_siblingof(&kid, &kid) == 0);
    }
    for (int i = 0; i < nprov; i++) {
	if (prov[i].done)
	    continue;
	fprintf(stderr, "cannot find DIE for %s %lx\n",
			prov[i].name, prov[i].sym.st_value);
    }
    return 0;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
