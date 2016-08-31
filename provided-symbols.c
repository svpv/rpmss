#include <stdio.h>
#include <string.h>
#include <assert.h>
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
    int nsym = dwfl_module_getsymtab(m);
    for (int i = 0; i < nsym; i++) {
	GElf_Sym sym;
	const char *name = dwfl_module_getsym(m, i, &sym, NULL);
	if (provided(&sym, name))
	    puts(name);
    }
    return 0;
}

/* ex: set ts=8 sts=4 sw=4 noet: */
