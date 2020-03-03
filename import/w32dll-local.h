/*
 * w32dll-local.h -- w32dll.c internal header file
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef W32DLL_LOCAL_H
#define W32DLL_LOCAL_H

/*************************************************************************/

/* Win-PE executable file structures. */

/************************************/

/* DOS executable header */

struct dos_header {
    uint16_t signature;         /* DOS_EXE_SIGNATURE */
    uint16_t len_bytes;         /* 1..512 */
    uint16_t len_sectors;
    uint16_t reloc_count;
    uint16_t len_header;
    uint16_t min_extra_mem;
    uint16_t max_extra_mem;
    uint16_t init_ss;
    uint16_t init_sp;
    uint16_t checksum;
    uint16_t init_cs;
    uint16_t init_ip;
    uint16_t reloc_offset;
    uint16_t overlay_num;
    uint8_t ignore1C[0x20];     /* 0x1C..0x3B */
    uint16_t winheader;
    uint16_t ignore3E;
};

#define DOS_EXE_SIGNATURE 0x5A4D  /* 'MZ' */

/************************************/

/* Win-PE executable headers (base and optional) */

enum rva_index {
    RVA_EXPORT = 0,
    RVA_IMPORT,
    RVA_RESOURCE,
    RVA_EXCEPTION,
    RVA_CERTIFICATE,
    RVA_BASE_RELOC,
    RVA_DEBUG,
    RVA_ARCH,
    RVA_GLOBAL_PTR,
    RVA_TLS_TABLE,
    RVA_LOAD_CONFIG,
    RVA_BOUND_IMPORT,
    RVA_IMPORT_ADDR,
    RVA_DELAY_IMPORT,
    RVA_MAX
};

struct pe_header {
    uint32_t signature;         /* WIN_PE_SIGNATURE */
    uint16_t arch;              /* WIN_PE_ARCH_xxx */
    uint16_t nsections;
    uint32_t timestamp;
    uint32_t sym_table_offset;
    uint32_t nsyms;
    uint16_t opt_header_size;
    uint16_t flags;             /* WIN_PE_FLAG_xxx */
};
struct pe_ext_header {
    uint16_t magic;             /* WIN_PE_OPT_MAGIC_xxx */
    uint8_t linkver_major;
    uint8_t linkver_minor;
    uint32_t code_size;
    uint32_t data_size;
    uint32_t bss_size;
    uint32_t entry_point;
    uint32_t code_base;
    uint32_t data_base;
    uint32_t image_base;        /* Code assumes it's loaded at this address */
    uint32_t section_align;
    uint32_t file_align;
    uint16_t osver_major;
    uint16_t osver_minor;
    uint16_t imagever_major;
    uint16_t imagever_minor;
    uint16_t subsysver_major;
    uint16_t subsysver_minor;
    uint32_t win32_ver;
    uint32_t image_size;
    uint32_t header_size;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_flags;         /* Ignored here */
    uint32_t stack_reserve;
    uint32_t stack_commit;
    uint32_t heap_reserve;
    uint32_t heap_commit;
    uint32_t loader_flags;
    uint32_t nrva;
    struct {
        uint32_t address;
        uint32_t size;
    } rva[RVA_MAX];
};

#define WIN_PE_SIGNATURE    0x00004550  /* 'PE\0\0' */

#define WIN_PE_ARCH_X86         0x014C  /* Ignore the lower 2 bits for this */
#define WIN_PE_ARCH_IA64        0x0200
#define WIN_PE_ARCH_X86_64      0x8664

#define WIN_PE_FLAG_DLL         0x2000

#define WIN_PE_OPT_MAGIC_32     0x010B  /* 32-bit code */
#define WIN_PE_OPT_MAGIC_64     0x020B  /* 64-bit code */

/************************************/

/* Section table */

struct pe_section_header {
    char name[8];
    uint32_t virtsize;
    uint32_t virtaddr;
    uint32_t filesize;
    uint32_t fileaddr;
    uint32_t reloc_table;
    uint32_t linenum_table;
    uint16_t nrelocs;
    uint16_t nlinenums;
    uint32_t flags;             /* SECTION_FLAG_xxx */
};

#define SECTION_FLAG_CODE   0x00000020
#define SECTION_FLAG_DATA   0x00000040
#define SECTION_FLAG_BSS    0x00000080
#define SECTION_FLAG_EXEC   0x20000000
#define SECTION_FLAG_READ   0x40000000
#define SECTION_FLAG_WRITE  0x80000000

/************************************/

/* Export directory */

struct export_directory {
    uint32_t flags;
    uint32_t timestamp;
    uint16_t version_major;
    uint16_t version_minor;
    uint32_t name;              /* Address of name in section */
    uint32_t ordinal_base;
    uint32_t nfuncs;
    uint32_t nnames;
    uint32_t func_table;
    uint32_t name_table;
    uint32_t name_ordinal_table;
};

/************************************/

/* Import directory */

struct import_directory {
    uint32_t import_table;
    uint32_t timestamp;
    uint32_t forward;
    uint32_t module_name;
    uint32_t import_addr_table;
};

struct import_name_entry {
    uint16_t hint;
    char name[1];       /* As long as necessary, null-terminated */
};

/*************************************************************************/

/* Constants for the DllMain() function (entry point). */
#define DLL_PROCESS_DETACH      0
#define DLL_PROCESS_ATTACH      1
#define DLL_THREAD_ATTACH       2
#define DLL_THREAD_DETACH       3

/* Handle value for "this module" (FIXME: assumes only one module). */
#define HANDLE_DEFAULT          1

/*************************************************************************/

/* Internal function prototypes. */

extern void *w32dll_emu_import_by_name(const char *module,
                                       const struct import_name_entry *name);
extern void *w32dll_emu_import_by_ordinal(const char *module,
                                          uint32_t ordinal);

/*************************************************************************/

#endif  /* W32DLL_LOCAL_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
