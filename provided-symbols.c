#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <dwarf.h>
#include <elfutils/libdwfl.h>

static bool provided(GElf_Sym *sym, const char *name)
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
    const char *ver;
    bool defaultver;
    bool done;
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

/* The elf header, to find out EI_CLASS and e_machine. */
static GElf_Ehdr ehdr;

/* Provided symbols. */
int nprov;
struct symx *prov = NULL;

/* Provided symbols are gathered from the ld.so's point of view.
 * That is, we use dynsym along with versym/verdef, not symtab. */
void gather_prov(Elf *elf)
{
    /* Get the elf header. */
    void *ret = gelf_getehdr(elf, &ehdr);
    assert(ret);
    /* Find sections. */
    Elf_Data *dynsym_data = NULL;
    Elf_Data *versym_data = NULL;
    Elf_Data *verdef_data = NULL;
    Elf32_Word dynsym_link = 0;
    Elf32_Word verdef_link = 0;
    Elf_Scn *scn = NULL;
    int nsym = 0;
    while (1) {
	scn = elf_nextscn(elf, scn);
	if (scn == NULL)
	    break;
	GElf_Shdr sh;
	ret = gelf_getshdr(scn, &sh);
	assert(ret);
	if (sh.sh_type == SHT_DYNSYM) {
	    dynsym_data = elf_getdata(scn, NULL);
	    dynsym_link = sh.sh_link;
	    assert(sh.sh_entsize > 0);
	    nsym = sh.sh_size / sh.sh_entsize;
	}
	else if (sh.sh_type == SHT_GNU_versym) {
	    versym_data = elf_getdata(scn, NULL);
	}
	else if (sh.sh_type == SHT_GNU_verdef) {
	    verdef_data = elf_getdata(scn, NULL);
	    verdef_link = sh.sh_link;
	}
    }
    assert(dynsym_data);
    assert(nsym > 0);
    /* Iterate symbols. */
    for (int i = 1; i < nsym; i++) {
	GElf_Sym sym;
	ret = gelf_getsym(dynsym_data, i, &sym);
	assert(ret);
	const char *name = elf_strptr(elf, dynsym_link, sym.st_name);
	assert(name);
	if (!provided(&sym, name))
	    continue;
	/* Deal with versioned symbols. */
	const char *ver = NULL;
	bool defaultver = false;
	if (versym_data) {
	    GElf_Versym versym;
	    ret = gelf_getversym(versym_data, i, &versym);
	    assert(ret);
	    defaultver = (versym & 0x8000) == 0;
	    versym &= 0x7fff;
	    assert(versym > 0);
	    if (versym > 1) {
		GElf_Verdef vd;
		size_t vdoff = 0;
		while (1) {
		    ret = gelf_getverdef(verdef_data, vdoff, &vd);
		    assert(ret);
		    if (vd.vd_ndx == versym)
			break; /* found */
		    assert(vd.vd_next > 0);
		    vdoff += vd.vd_next;
		}
		GElf_Verdaux verdaux;
		ret = gelf_getverdaux(verdef_data, vdoff + vd.vd_aux, &verdaux);
		assert(ret);
		ver = elf_strptr(elf, verdef_link, verdaux.vda_name);
	    }
	}
	/* Add symbol. */
	int delta = 1024;
	if ((nprov & (delta - 1)) == 0)
	    prov = realloc(prov, sizeof(*prov) * (nprov + delta));
	prov[nprov++] = (struct symx) { sym, name, ver, defaultver, false };
    }
    assert(nprov > 0);
    qsort(prov, nprov, sizeof(*prov), provcmp);
}

static const char *getname(Dwarf_Die *die)
{
    Dwarf_Attribute attr;
    if (dwarf_attr(die, DW_AT_name, &attr) == NULL)
	return NULL;
    return dwarf_formstring(&attr);
}

static Dwarf_Die *gettype(Dwarf_Die *var, Dwarf_Die *type)
{
    Dwarf_Attribute attr;
    if (dwarf_attr_integrate(var, DW_AT_type, &attr) == NULL)
	return NULL;
    if (dwarf_formref_die(&attr, type) == NULL)
	return NULL;
    if (dwarf_peel_type(type, type) != 0)
	return NULL;
    return type;
}

static char *puttype(char *p, Dwarf_Die *type, bool arg)
{
    /* In function arguments, arrays decay into pointers.
     * Otherwise, array type depends on its element. */
    if (!arg && dwarf_tag(type) == DW_TAG_array_type) {
	*p++ = 'a';
	if (gettype(type, type) == NULL)
	    return NULL;
    }
    switch (dwarf_tag(type)) {
    case DW_TAG_array_type:
    case DW_TAG_pointer_type:
	*p++ = 'p';
	return p;
    }
    Dwarf_Word size;
    if (dwarf_aggregate_size(type, &size) != 0 || size < 1)
	return NULL;
    Dwarf_Word wordsize = 4 * ehdr.e_ident[EI_CLASS];
    assert(wordsize > 0);
    switch (dwarf_tag(type)) {
    case DW_TAG_enumeration_type:
	goto intx;
    case DW_TAG_base_type:
	break;
    default:
	goto blob;
    }
    Dwarf_Attribute attr;
    if (dwarf_attr(type, DW_AT_encoding, &attr) == NULL)
	return NULL;
    Dwarf_Word enc;
    if (dwarf_formudata(&attr, &enc) != 0)
	return NULL;
    switch (enc) {
    case DW_ATE_boolean:
    case DW_ATE_signed:
    case DW_ATE_unsigned:
    case DW_ATE_signed_char:
    case DW_ATE_unsigned_char:
intx:	/* Arguments are passed in full words, via registers
	 * or due to stack alignment. */
	if (arg && size <= wordsize)
	    *p++ = 'i';
	else
	    p += sprintf(p, "i%lu", size);
	return p;
    case DW_ATE_float:
	/* On x86, sizeof(80-bit long double) == 12.
	 * For our purposes, 10 looks more natural. */
	if (size == 12 && ehdr.e_machine == EM_386)
	    size = 10;
	/* On x86-64, sizeof(80-bit long double) == 16. */
	if (size == 16 && ehdr.e_machine == EM_X86_64) {
	    const char *name = getname(type);
	    if (name && strcmp(name, "long double") == 0)
		size = 10;
	}
	p += sprintf(p, "f%lu", size);
	return p;
    }
blob:
    p += sprintf(p, "b%lu", size);
    return p;
}

static char *funcproto(Dwarf_Die *die, const char *name)
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
	Dwarf_Die type;
	if (gettype(&kid, &type) == NULL) {
noarg:	    fprintf(stderr, "cannot parse args for %s\n", name);
	    return NULL;
	}
	if (p > buf + 1) {
	    *p++ = ',';
	    *p++ = ' ';
	}
	p = puttype(p, &type, 1);
	if (p == NULL)
	    goto noarg;
    }
    while (dwarf_siblingof(&kid, &kid) == 0);
ret:
    *p++ = ')';
    *p++ = '\0';
    return buf;
}

static char *varproto(Dwarf_Die *die, const char *name)
{
    static char buf[128];
    char *p = buf;
    Dwarf_Die type;
    if (gettype(die, &type) == NULL) {
notype: fprintf(stderr, "cannot parse type for %s\n", name);
	return NULL;
    }
    p = puttype(p, &type, 0);
    if (p == NULL)
	goto notype;
    *p++ = '\0';
    return buf;
}

static void print_sym_0(const char *what, const char *name,
			const char *ver, const char *proto)
{
    printf("%s\t%s%s%s%s%s%s", what, name,
	    ver ? "@" : "",
	    ver ? ver : "",
	    proto ? "\t" : "\n",
	    proto ? proto : "",
	    proto ? "\n" : "");
}

/* For default symbol foo@@VER, also print foo without VER. */
static bool compat_nover;

/* For foo(proto), also print foo without proto. */
static bool compat_noproto;

static void print_sym_1(const char *what, struct symx *symx, const char *proto)
{
    print_sym_0(what, symx->name, symx->ver, proto);
    if (symx->ver && symx->defaultver && compat_nover)
	print_sym_0(what, symx->name, NULL, proto);
}

static void print_sym_2(const char *what, struct symx *symx, Dwarf_Die *die,
			char *(*getproto)(Dwarf_Die *die, const char *name))
{
    const char *proto = NULL;
    /* We do not use proto for mangled symbols. */
    if (!(symx->name[0] == '_' && symx->name[1] == 'Z'))
	proto = getproto(die, symx->name);
    print_sym_1(what, symx, proto);
    if (proto && compat_noproto)
	print_sym_1(what, symx, NULL);
}

static void print_func(struct symx *symx, Dwarf_Die *die)
{
    print_sym_2("func", symx, die, funcproto);
}

static void print_var(struct symx *symx, Dwarf_Die *die)
{
    print_sym_2("var", symx, die, varproto);
}

#include <getopt.h>

static bool verbose;

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
    gather_prov(elf);

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
	    struct symx *symx = bsearch(&key, prov, nprov, sizeof(*prov), provcmp);
	    if (!symx) {
		const char *name = getname(&kid);
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
		    print_var(symx, &kid);
		symx->done = true;
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
