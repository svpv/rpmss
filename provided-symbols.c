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

int main(int argc, char **argv)
{
    assert(argc == 2);
    static const Dwfl_Callbacks cb = {
	.section_address = dwfl_offline_section_address,
	.find_debuginfo	 = dwfl_standard_find_debuginfo,
	.find_elf	 = dwfl_build_id_find_elf,
    };
    Dwfl *dwfl = dwfl_begin(&cb);
    assert(dwfl);
    Dwfl_Module *m = dwfl_report_offline(dwfl, argv[1], argv[1], -1);
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
		if (name)
		    fprintf(stderr, "nothing for %s %s %lx\n",
				    tag == DW_TAG_subprogram ? "func" : "var",
				    name, key.sym.st_value);
		continue;
	    }
	    while (symx > prov && symx[-1].sym.st_value == key.sym.st_value)
		symx--;
	    do {
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
