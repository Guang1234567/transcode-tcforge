/*
 * w32dll.c -- a simplistic interface to Win32 DLLs (no thread support)
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>

#if !defined(HAVE_MMAP)
# error Sorry, mmap() support is required.
#endif

#if defined(HAVE_ENDIAN_H)
# include <endian.h>
# if __BYTE_ORDER != __LITTLE_ENDIAN
#  error Sorry, only little-endian architectures are supported.
# endif
#endif

#if defined(HAVE_SYSCONF_WITH_SC_PAGESIZE)
# define GETPAGESIZE() (sysconf(_SC_PAGESIZE))
#elif defined(HAVE_GETPAGESIZE)
# define GETPAGESIZE() (getpagesize())
#elif defined(PAGESIZE)
# define GETPAGESIZE() (PAGESIZE)
#elif defined(PAGE_SIZE)
# define GETPAGESIZE() (PAGE_SIZE)
#else
# error System page size is not available!
#endif

#include "w32dll.h"
#include "w32dll-local.h"

/*************************************************************************/

/* Contents of a DLL handle. */

struct w32dllhandle_ {
    /* Signature (to protect against bad pointers and double-free */
    uint32_t signature;

    /* Overall file data */
    struct pe_header header;
    struct pe_ext_header extheader;

    /* File position for each RVA entry */
    off_t rva_filepos[RVA_MAX];

    /* Data for each loaded section */
    int nsections;
    struct section_info {
        void *base;
        uint32_t size;
        int prot;               /* Protection flags for mprotect() */
        uint32_t origbase;      /* Virtual address given in section header */
        uint32_t origsize;      /* Likewise, for size */
    } *sections;

    /* Data for exported functions */
    int export_ordinal_base;
    int export_ordinal_count;
    void **export_table;
    int export_name_count;
    struct export_name {
        char *name;
        uint32_t ordinal;
    } *export_name_table;
};

#define HANDLE_SIGNATURE    0xD11DA7A5


/* Forward declarations for internal routines. */

static int w32dll_add_section(W32DLLHandle dll, int fd,
                              struct pe_section_header *secthdr);
static int w32dll_load_section(int fd, struct pe_section_header *secthdr,
                               struct section_info *sectinfo);
static void w32dll_update_rva(W32DLLHandle dll,
                              struct pe_section_header *secthdr);
static int w32dll_read_exports(W32DLLHandle dll, int fd);
static int w32dll_process_imports(W32DLLHandle dll,
                                  struct import_directory *importdir);
static void *w32dll_import_by_name(const char *module,
                                   const struct import_name_entry *name);
static void *w32dll_import_by_ordinal(const char *module, uint32_t ordinal);
static int w32dll_read_relocs(W32DLLHandle dll, int fd,
                              uint32_t **relocs_ptr, int *nrelocs_ptr);
static void w32dll_relocate(W32DLLHandle dll, uint32_t *relocs, int nrelocs);
static void *w32dll_relocate_addr(W32DLLHandle dll, uint32_t addr);
static char *w32dll_read_asciiz(int fd);
static int w32dll_init_fs(void);

/*************************************************************************/
/*************************************************************************/

/* External interface routines. */

/*************************************************************************/

/**
 * w32dll_load:  Load the given DLL file into memory, and return a handle
 * to it.
 *
 * Parameters:
 *       path: DLL file pathname.
 *     compat: If nonzero, adds a memory mapping for the entire DLL to
 *             accommodate misbehaving DLLs that access memory outside the
 *             registered sections.
 * Return value:
 *     DLL handle (nonzero), or zero on error.
 * Side effects:
 *     Sets errno to an appropriate value on error, including ENOEXEC if
 *     the file is not recognized as a Win32 DLL file or is corrupt or
 *     truncated, or ETXTBSY if the DLL's DllMain() function returns an
 *     error.  On successful return, errno is undefined.
 */

W32DLLHandle w32dll_load(const char *path, int compat)
{
    W32DLLHandle dll;
    struct dos_header doshdr;
    int fd, i;

    /* Allocate and initialize the DLL handle. */
    dll = malloc(sizeof(*dll));
    if (!dll)
        return NULL;
    memset(&dll->header, 0, sizeof(dll->header));
    memset(&dll->extheader, 0, sizeof(dll->extheader));
    memset(&dll->rva_filepos, 0, sizeof(dll->rva_filepos));
    dll->signature            = HANDLE_SIGNATURE;
    dll->nsections            = 0;
    dll->sections             = NULL;
    dll->export_ordinal_base  = 0;
    dll->export_ordinal_count = 0;
    dll->export_table         = NULL;
    dll->export_name_count    = 0;
    dll->export_name_table    = NULL;

    /* Open the file, and ensure that it's seekable. */
    fd = open(path, O_RDONLY);
    if (fd == -1 || lseek(fd, 0, SEEK_SET) == -1) {
        int errno_save = errno;
        free(dll);
        errno = errno_save;
        return NULL;
    }

    /* Check for a valid (Win32-style) DOS executable header. */
    if (read(fd, &doshdr, sizeof(doshdr)) != sizeof(doshdr)
     || doshdr.signature != DOS_EXE_SIGNATURE
     || doshdr.reloc_offset < 0x40
    ) {
        goto err_noexec;
    }

    /* Check for a valid PE header (standard and optional both required). */
    if (lseek(fd, doshdr.winheader, SEEK_SET) == -1
     || read(fd, &dll->header, sizeof(dll->header)) != sizeof(dll->header)
     || dll->header.opt_header_size < sizeof(dll->extheader)
     || read(fd, &dll->extheader, sizeof(dll->extheader))
                                                  != sizeof(dll->extheader)
     || dll->header.signature != WIN_PE_SIGNATURE
     || !(dll->header.flags & WIN_PE_FLAG_DLL)
#if defined(ARCH_X86)
     || (dll->header.arch & ~3) != WIN_PE_ARCH_X86
     || dll->extheader.magic != WIN_PE_OPT_MAGIC_32
#else
# error Sorry, this architecture is not supported.
#endif
    ) {
        goto err_noexec;
    }
    /* Skip past any extra header bytes we didn't need. */
    if (dll->header.opt_header_size > sizeof(dll->extheader)) {
        if (lseek(fd, dll->header.opt_header_size - sizeof(dll->extheader),
                  SEEK_CUR) == -1
        ) {
            goto err_noexec;
        }
    }

    /* Go through the section table and attempt to load each section.  Also
     * determine file positions for each RVA entry.  Note that we do not
     * simply map the entire file because (1) sections may be larger in
     * memory than in the file and (2) the system's page size may be larger
     * than that specified in the file. */
    for (i = 0; i < dll->header.nsections + (compat ? 1 : 0); i++) {
        struct pe_section_header secthdr;

        if (i >= dll->header.nsections) {
            /* Set up compatibility entry */
            off_t curpos = lseek(fd, 0, SEEK_CUR);
            off_t filesize = lseek(fd, 0, SEEK_END);
            if (curpos==-1 || filesize==-1 || lseek(fd,curpos,SEEK_SET)==-1)
                goto error;
            secthdr.virtaddr = 0;
            secthdr.virtsize = dll->extheader.image_size;
            secthdr.fileaddr = 0;
            secthdr.filesize = filesize;
            secthdr.flags = SECTION_FLAG_DATA | SECTION_FLAG_READ;
        } else {
            if (read(fd, &secthdr, sizeof(secthdr)) != sizeof(secthdr)) 
                goto err_noexec;
        }
        w32dll_update_rva(dll, &secthdr);
        w32dll_add_section(dll, fd, &secthdr);
    }

    /* Load and process relocations.  Note that once the sections are
     * loaded, we could theoretically just retrieve these (and the other
     * data below) from memory, but since we take the approach of only
     * loading/mapping the sections we need, we do this the hard way and
     * read the data directly from the file. */
    if (dll->rva_filepos[RVA_BASE_RELOC]
     && dll->extheader.rva[RVA_BASE_RELOC].size
    ) {
        uint32_t *relocs = NULL;
        int nrelocs = 0;

        if (lseek(fd, dll->rva_filepos[RVA_BASE_RELOC], SEEK_SET) == -1)
            goto error;
        while (lseek(fd, 0, SEEK_CUR)
               <= dll->rva_filepos[RVA_BASE_RELOC]
                  + dll->extheader.rva[RVA_BASE_RELOC].size - 8
        ) {
            int res = w32dll_read_relocs(dll, fd, &relocs, &nrelocs);
            if (res < 0)
                goto error;
            if (res == 0)
                break;
        }
        w32dll_relocate(dll, relocs, nrelocs);
    }

    /* Load export table. */
    if (dll->rva_filepos[RVA_EXPORT]
     && dll->extheader.rva[RVA_EXPORT].size >= sizeof(struct export_directory)
    ) {
        if (!w32dll_read_exports(dll, fd))
            goto error;
    }

    /* Load and process import table. */
    if (dll->rva_filepos[RVA_IMPORT]
     && dll->extheader.rva[RVA_IMPORT].size >= sizeof(struct import_directory)
    ) {
        struct import_directory importdir;

        if (lseek(fd, dll->rva_filepos[RVA_IMPORT], SEEK_SET) == -1)
            goto error;
        while (lseek(fd, 0, SEEK_CUR)
               <= dll->rva_filepos[RVA_IMPORT]
                  + dll->extheader.rva[RVA_IMPORT].size - sizeof(importdir)
        ) {
            if (read(fd, &importdir, sizeof(importdir)) != sizeof(importdir))
                goto err_noexec;
            if (!importdir.module_name)
                break;  /* Last entry in table */
            if (!importdir.import_table || !importdir.import_addr_table)
                goto err_noexec;
            if (!w32dll_process_imports(dll, &importdir))
                goto error;
        }
    }

    /* Set section access privileges appropriately. */
    for (i = 0; i < dll->nsections; i++) {
        if (mprotect(dll->sections[i].base, dll->sections[i].size,
                     dll->sections[i].prot) != 0
        ) {
            goto error;
        }
    }

    /* Close file descriptor (no longer needed). */
    close(fd);
    fd = -1;

    /* Set up the FS register with a dummy thread information block.
     * We deliberately don't support libraries that depend on the OS to
     * put things here; we just provide the space so that accesses to
     * %fs:... don't segfault. */
    if (!w32dll_init_fs())
        goto error;

    /* Call the DllMain() entry point. */
    if (dll->extheader.entry_point) {
        WINAPI int (*DllMain)(uint32_t handle, uint32_t reason, void *resv);
        DllMain = w32dll_relocate_addr(dll, dll->extheader.entry_point
                                            + dll->extheader.image_base);
        if (!DllMain)
            goto err_noexec;
        if (!(*DllMain)(HANDLE_DEFAULT, DLL_PROCESS_ATTACH, NULL)) {
            (*DllMain)(HANDLE_DEFAULT, DLL_PROCESS_DETACH, NULL);
            errno = ETXTBSY;
            goto error;
        }
    }

    /* Successful! */
    return dll;

    /* Error handling */
  err_noexec:
    errno = ENOEXEC;
  error:
    {
        int errno_save = errno;
        close(fd);
        w32dll_unload(dll);
        errno_save = errno;
        return NULL;
    }
}

/*************************************************************************/

/**
 * w32dll_unload:  Unload the given DLL from memory.  Does nothing if the
 * given handle is zero or invalid.
 *
 * Parameters:
 *     dll: DLL handle.
 * Return value:
 *     None.
 */

void w32dll_unload(W32DLLHandle dll)
{
    int i;

    if (!dll || dll->signature != HANDLE_SIGNATURE)
        return;

    /* Call the DllMain() entry point with DLL_PROCESS_DETACH. */
    if (dll->extheader.entry_point) {
        WINAPI int (*DllMain)(uint32_t handle, uint32_t reason, void *resv);
        DllMain = w32dll_relocate_addr(dll, dll->extheader.entry_point
                                            + dll->extheader.image_base);
        if (DllMain)
            (*DllMain)(HANDLE_DEFAULT, DLL_PROCESS_DETACH, NULL);
    }

    /* Free DLL memory. */
    for (i = 0; i < dll->nsections; i++) {
        munmap(dll->sections[i].base, dll->sections[i].size);
        dll->sections[i].base = NULL;
        dll->sections[i].size = 0;
    }
    free(dll->sections);
    dll->sections = NULL;
    dll->nsections = 0;

    /* Free export tables. */
    free(dll->export_table);
    dll->export_table = NULL;
    for (i = 0; i < dll->export_name_count; i++) {
        free(dll->export_name_table[i].name);
        dll->export_name_table[i].name = NULL;
    }
    free(dll->export_name_table);
    dll->export_name_table = NULL;

    /* Free the handle structure itself. */
    dll->signature = ~HANDLE_SIGNATURE;
    free(dll);

    return;
}

/*************************************************************************/

/**
 * w32dll_lookup_by_name:  Look up the address of an exported function in
 * the given DLL, using the function's name.
 *
 * Parameters:
 *      dll: DLL handle.
 *     name: Function name.
 * Return value:
 *     Function address, or NULL on error.
 * Side effects:
 *     Sets errno to one of the following values on error:
 *         EINVAL: `dll' or `name' was invalid.
 *         ENOENT: The requested function does not exist.
 *     On successful return, errno is undefined.
 */

void *w32dll_lookup_by_name(W32DLLHandle dll, const char *name)
{
    int i;

    if (!dll || dll->signature != HANDLE_SIGNATURE || !name || !*name) {
        errno = EINVAL;
        return NULL;
    }
    for (i = 0; i < dll->export_name_count; i++) {
        if (strcmp(name, dll->export_name_table[i].name) == 0) {
            return w32dll_lookup_by_ordinal(dll,
                                            dll->export_name_table[i].ordinal);
        }
    }
    errno = ENOENT;
    return NULL;
}

/*************************************************************************/

/**
 * w32dll_lookup_by_ordinal:  Look up the address of an exported function
 * in the given DLL, using the function's ordinal value.
 *
 * Parameters:
 *         dll: DLL handle.
 *     ordinal: Function ordinal.
 * Return value:
 *     Function address, or NULL on error.
 * Side effects:
 *     Sets errno to one of the following values on error:
 *         EINVAL: `dll' was invalid.
 *         ENOENT: The requested function does not exist.
 *     On successful return, errno is undefined.
 */

void *w32dll_lookup_by_ordinal(W32DLLHandle dll, uint32_t ordinal)
{
    if (!dll || dll->signature != HANDLE_SIGNATURE) {
        errno = EINVAL;
        return NULL;
    }
    if (ordinal < dll->export_ordinal_base) {
        errno = ENOENT;
        return NULL;
    }
    ordinal -= dll->export_ordinal_base;
    if (ordinal >= dll->export_ordinal_count || !dll->export_table[ordinal]) {
        errno = ENOENT;
        return NULL;
    }
    return dll->export_table[ordinal];
}

/*************************************************************************/
/*************************************************************************/

/* Internal routines. */

/*************************************************************************/

/**
 * w32dll_add_section:  Checks the given section description, and loads it
 * in from the DLL file, appending information to the dll->sections[]
 * array, if appropriate.
 *
 * Parameters:
 *         dll: DLL handle.
 *          fd: File descriptor to read from.
 *     secthdr: Pointer to section header.
 * Return value:
 *     Nonzero on success (including when the section was intentionally
 *     not loaded), zero on error.
 * Notes:
 *     - On success, the file's current offset is not changed.
 *     - The allocated memory will be marked read/write; after relocation,
 *           use mprotect() to set the protection to sectinfo->prot.
 *     - On error, errno is set appropriately.
 */

static int w32dll_add_section(W32DLLHandle dll, int fd,
                              struct pe_section_header *secthdr)
{
    void *new_sections;

    if (!(secthdr->flags & (SECTION_FLAG_CODE
                          | SECTION_FLAG_DATA
                          | SECTION_FLAG_BSS))
    ) {
        /* Don't know what kind of section this is, but we don't need it */
        return 1;
    }

    if (!(secthdr->flags & (SECTION_FLAG_READ
                          | SECTION_FLAG_WRITE
                          | SECTION_FLAG_EXEC))
    ) {
        /* Don't bother loading--it wouldn't be accessible anyway */
        return 1;
    }

    new_sections = realloc(dll->sections,
                           sizeof(*dll->sections) * (dll->nsections+1));
    if (!new_sections)
        return 0;
    dll->sections = new_sections;
    dll->sections[dll->nsections].base = NULL;
    dll->sections[dll->nsections].size = 0;
    dll->sections[dll->nsections].prot = 0;
    dll->sections[dll->nsections].origbase = 0;
    dll->sections[dll->nsections].origsize = 0;
    dll->nsections++;
    if (!w32dll_load_section(fd, secthdr, &dll->sections[dll->nsections-1]))
        return 0;
    dll->sections[dll->nsections-1].origbase += dll->extheader.image_base;

    return 1;
}

/*************************************************************************/

/**
 * w32dll_load_section:  Loads the section described by `secthdr' from the
 * file descriptor `fd', setting the `sectinfo' structure appropriately.
 *
 * Parameters:
 *           fd: File to load data from.
 *      secthdr: Section header loaded from the file.
 *     sectinfo: Structure to store information about the segment in.
 * Return value:
 *     Nonzero on success, zero on error.
 * Notes:
 *     - On success, the file's current offset is not changed.
 *     - The allocated memory will be marked read/write; after relocation,
 *           use mprotect() to set the protection to sectinfo->prot.
 *     - On error, errno is set appropriately.
 */

static int w32dll_load_section(int fd, struct pe_section_header *secthdr,
                               struct section_info *sectinfo)
{
    int newfd;
    void *base;
    uint32_t size, toread;
    off_t oldofs;

    uint32_t pagesize = GETPAGESIZE();
    if (pagesize < 0) {
        errno = EINVAL;
        return 0;
    }

#ifdef MAP_ANONYMOUS
    newfd = -1;
#else
    newfd = open("/dev/zero", O_RDWR);
#endif
    size = (secthdr->virtsize + pagesize-1) / pagesize * pagesize;
    base = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE
#ifdef MAP_ANONYMOUS
                                                    | MAP_ANONYMOUS
#endif
                , newfd, 0);
#ifndef MAP_ANONYMOUS
    if (newfd != -1) {
        int errno_save = errno;
        close(newfd);
        errno = errno_save;
    }
#endif
    if (base == MAP_FAILED)
        return 0;

    oldofs = lseek(fd, 0, SEEK_CUR);
    if (oldofs == -1) {
        munmap(base, size);
        return 0;
    }
    if (secthdr->filesize < secthdr->virtsize)
        toread = secthdr->filesize;
    else
        toread = secthdr->virtsize;
    if (lseek(fd, secthdr->fileaddr, SEEK_SET) == -1
     || read(fd, base, toread) != toread
     || lseek(fd, oldofs, SEEK_SET) == -1
    ) {
        munmap(base, size);
        errno = ENOEXEC;
        return 0;
    }

    sectinfo->base = base;
    sectinfo->size = size;
    sectinfo->prot = 0;
    if (secthdr->flags & SECTION_FLAG_READ)
        sectinfo->prot |= PROT_READ;
    if (secthdr->flags & SECTION_FLAG_WRITE)
        sectinfo->prot |= PROT_WRITE;
    if (secthdr->flags & SECTION_FLAG_EXEC)
        sectinfo->prot |= PROT_EXEC;
    sectinfo->origbase = secthdr->virtaddr;
    sectinfo->origsize = secthdr->virtsize;

    return 1;
}

/*************************************************************************/

/**
 * w32dll_update_rva:  Update the rva_filepos[] table in the DLL handle
 * for any RVAs within the given segment.
 *
 * Parameters:
 *         dll: DLL handle.
 *     secthdr: Pointer to section header.
 * Return value:
 *     None.
 */

static void w32dll_update_rva(W32DLLHandle dll,
                              struct pe_section_header *secthdr)
{
    int i;

    for (i = 0; i < RVA_MAX; i++) {
        if (!dll->rva_filepos[i]
         && dll->extheader.rva[i].address >= secthdr->virtaddr
         && dll->extheader.rva[i].address < secthdr->virtaddr+secthdr->virtsize
        ) {
            dll->rva_filepos[i] =
                dll->extheader.rva[i].address - secthdr->virtaddr
                                              + secthdr->fileaddr;
        }
    }
}

/*************************************************************************/

/**
 * w32dll_read_exports:  Read in the DLL's export table, and fill in the
 * export data in the DLL handle.
 *
 * Parameters:
 *     dll: DLL handle.
 *      fd: File descriptor to read from.
 * Return value:
 *     Nonzero on success, zero on failure.
 * Notes:
 *     On error, errno is set appropriately.
 */

static int w32dll_read_exports(W32DLLHandle dll, int fd)
{
    struct export_directory exportdir;
    off_t secofs = (off_t)dll->rva_filepos[RVA_EXPORT]
                   - (off_t)dll->extheader.rva[RVA_EXPORT].address;

    /* Read in the export table. */
    if (lseek(fd, dll->rva_filepos[RVA_EXPORT], SEEK_SET) == -1)
        goto error;
    if (read(fd, &exportdir, sizeof(exportdir)) != sizeof(exportdir))
        goto err_noexec;
    dll->export_ordinal_base = exportdir.ordinal_base;

    /* Read in each exported function address, relocate it, and store the
     * relocated address in the DLL handle structure. */
    if (exportdir.nfuncs) {
        dll->export_table =
            malloc(sizeof(*dll->export_table) * exportdir.nfuncs);
        if (!dll->export_table)
            goto error;
        if (lseek(fd, exportdir.func_table + secofs, SEEK_SET) == -1)
            goto error;
        /* Use the entry count field itself as a loop variable, to ensure
         * that the correct number of entries are cleaned up on error
         * (doesn't matter here, but avoids a memory leak for the name
         * array handling below) */
        for (dll->export_ordinal_count = 0;
             dll->export_ordinal_count < exportdir.nfuncs;
             dll->export_ordinal_count++
        ) {
            uint32_t address;
            if (read(fd, &address, 4) != 4)
                goto err_noexec;
            address += dll->extheader.image_base;
            dll->export_table[dll->export_ordinal_count] =
                w32dll_relocate_addr(dll, address);
        }
    }

    /* Read in each exported function name, and store the name and its
     * associated ordinal in the DLL handle structure. */
    if (exportdir.nnames) {
        int i;
        dll->export_name_table =
            malloc(sizeof(*dll->export_name_table) * exportdir.nnames);
        if (!dll->export_name_table)
            goto error;
        if (lseek(fd, exportdir.name_ordinal_table + secofs, SEEK_SET) == -1)
            goto error;
        for (i = 0; i < exportdir.nnames; i++) {
            uint16_t ordinal;
            if (read(fd, &ordinal, 2) != 2)
                goto err_noexec;
            dll->export_name_table[i].ordinal =
                dll->export_ordinal_base + ordinal;
        }
        for (dll->export_name_count = 0;
             dll->export_name_count < exportdir.nnames;
             dll->export_name_count++
        ) {
            uint32_t name_address;
            char *s;
            if (lseek(fd, exportdir.name_table+secofs+dll->export_name_count*4,
                      SEEK_SET) == -1
            ) {
                goto error;
            }
            if (read(fd, &name_address, 4) != 4)
                goto err_noexec;
            if (lseek(fd, name_address + secofs, SEEK_SET) == -1)
                goto error;
            s = w32dll_read_asciiz(fd);
            if (!s)
                goto error;
            dll->export_name_table[dll->export_name_count].name = s;
        }
    }

    /* Success! */
    return 1;

  err_noexec:
    errno = ENOEXEC;
  error:
    return 0;
}

/*************************************************************************/

/**
 * w32dll_process_imports:  Reads the list of imports described by
 * `importdir' and sets the pointers to appropriate values (emulation
 * functions or a placeholder function).
 *
 * Parameters:
 *           dll: DLL handle.
 *     importdir: Import directory structure.
 * Return value:
 *     Nonzero on success, zero on error.
 * Notes:
 *     - On error, errno is set appropriately.
 *     - This routine assumes that all import data is located in the same
 *       section.  Since the import address table has to be in a loaded
 *       section (usually a data section), this implies that the rest of
 *       the import data is also in a loaded section; therefore, we take
 *       the easy approach and access the data directly in memory.
 */

static int w32dll_process_imports(W32DLLHandle dll,
                                  struct import_directory *importdir)
{
    const uint32_t imgbase = dll->extheader.image_base;  // shorthand
    const char *module;
    const struct import_name_entry **names;
    void **addrs;
    int i;

    /* Relocate import directory addresses. */
    module = w32dll_relocate_addr(dll, importdir->module_name + imgbase);
    names = w32dll_relocate_addr(dll, importdir->import_table + imgbase);
    addrs = w32dll_relocate_addr(dll, importdir->import_addr_table + imgbase);
    if (!module || !*module || !names || !addrs)
        goto err_noexec;

    /* Process the imports. */
    for (i = 0; names[i]; i++) {
        const struct import_name_entry *name;
        uint32_t ordinal;
        if ((uint32_t)names[i] & 0x80000000UL) {
            name = NULL;
            ordinal = (uint32_t)names[i] & 0x7FFFFFFFUL;
        } else {
            name = w32dll_relocate_addr(dll, (uint32_t)names[i] + imgbase);
            if (!name)
                goto err_noexec;
        }
        if (name)
            addrs[i] = w32dll_import_by_name(module, name);
        else
            addrs[i] = w32dll_import_by_ordinal(module, ordinal);
    }

    /* All done. */
    return 1;

  err_noexec:
    errno = ENOEXEC;
    return 0;
}

/*************************************************************************/

/**
 * w32dll_import_by_name, w32dll_import_by_ordinal:  Return the address
 * corresponding to the given import, selected by either name or ordinal.
 *
 * Parameters:
 *      module: Name of the module from which to import.
 *        name: Import name descriptor (w32dll_import_by_name() only).
 *     ordinal: Import ordinal (w32dll_import_by_ordinal() only).
 * Return value:
 *     The address corresponding to the import, or NULL if the import
 *     failed.
 * Notes:
 *     - A NULL return is *not* considered an error, and thus errno is
 *       undefined after returning from these functions.
 *     - Currently, these functions just ask the Win32 emulation layer for
 *       an appropriate function, and do not handle linking between
 *       multiple loaded DLLs.
 */

static void *w32dll_import_by_name(const char *module,
                                   const struct import_name_entry *name)
{
    return w32dll_emu_import_by_name(module, name);
}

/************************************/

static void *w32dll_import_by_ordinal(const char *module, uint32_t ordinal)
{
    return w32dll_emu_import_by_ordinal(module, ordinal);
}

/*************************************************************************/

/**
 * w32dll_read_relocs:  Read a set of relocation offsets for the DLL from
 * the given file descriptor.
 *
 * Parameters:
 *             dll: DLL handle.
 *              fd: File descriptor to read from.
 *      relocs_ptr: Pointer to relocation entry array (dynamically allocated).
 *     nrelocs_ptr: Pointer to relocation entry count.
 * Return value:
 *     Positive if relocations were read successfully.
 *     Zero if the end of the relocation table was reached.
 *     Negative if an error cocurred.
 * Notes:
 *     - *relocs_ptr and *nrelocs_ptr must be initialized to NULL and 0,
 *       respectively, before the first call; they will be updated with
 *       each call.
 *     - On error, errno is set appropriately.
 */

static int w32dll_read_relocs(W32DLLHandle dll, int fd,
                              uint32_t **relocs_ptr, int *nrelocs_ptr)
{
    uint32_t base, size, *new_relocs;
    int index;

    if (read(fd, &base, 4) != 4
     || read(fd, &size, 4) != 4
     || (size > 0 && size < 8)
    ) {
        free(*relocs_ptr);
        *relocs_ptr = NULL;
        *nrelocs_ptr = 0;
        errno = ENOEXEC;
        return -1;
    }
    if (!size)
        return 0;
    if (size <= 8)  // Technically == works too, but play it safe
        return 1;
    size = (size-8) / 2;  // Number of entries in this group
    index = *nrelocs_ptr;
    new_relocs = realloc(*relocs_ptr,
                         sizeof(**relocs_ptr) * (*nrelocs_ptr + size));
    if (!new_relocs)
        goto err_noexec;
    *relocs_ptr = new_relocs;
    *nrelocs_ptr += size;
    while (size > 0) {
        uint16_t buf[1000];
        int toread, i;
        toread = size;
        if (toread > sizeof(buf)/2)
            toread = sizeof(buf)/2;
        if (read(fd, buf, toread*2) != toread*2)
            goto err_noexec;
        for (i = 0; i < toread; i++) {
            if (buf[i]>>12 == 3) {
                (*relocs_ptr)[index++] = dll->extheader.image_base
                                       + base + (buf[i] & 0xFFF);
            } else {
                (*nrelocs_ptr)--;
            }
        }
        size -= toread;
    }
    return 1;

  err_noexec:
    {
        int errno_save = errno;
        free(*relocs_ptr);
        *relocs_ptr = NULL;
        *nrelocs_ptr = 0;
        errno = errno_save;
        return -1;
    }
}

/*************************************************************************/

/**
 * w32dll_relocate:  Perform relocations on the loaded DLL.
 *
 * Parameters:
 *         dll: DLL handle with all sections loaded.
 *      relocs: Array of virtual addresses to be relocated.
 *     nrelocs: Number of relocations.
 * Return value:
 *     None.
 */

#include <stdio.h>
static void w32dll_relocate(W32DLLHandle dll, uint32_t *relocs, int nrelocs)
{
    int i;

    for (i = 0; i < nrelocs; i++) {
        uint32_t *addr = w32dll_relocate_addr(dll, relocs[i]);
        if (addr)
            *addr = (uint32_t)w32dll_relocate_addr(dll, *addr);
    }
}

/*************************************************************************/

/**
 * w32dll_relocate_addr:  Relocate a single address.
 *
 * Parameters:
 *      dll: DLL handle.
 *     addr: Address to relocate.
 * Return value:
 *     The relocated address, or NULL if the address is not in a loaded
 *     section.
 */

static void *w32dll_relocate_addr(W32DLLHandle dll, uint32_t addr)
{
    int i;

    for (i = 0; i < dll->nsections; i++) {
        if (addr >= dll->sections[i].origbase
         && addr <  dll->sections[i].origbase + dll->sections[i].origsize
        ) {
            return (uint8_t *)dll->sections[i].base
                   + (addr - dll->sections[i].origbase);
        }
    }
    return NULL;
}

/*************************************************************************/

/**
 * w32dll_read_asciiz:  Read a null-terminated string from the given file
 * descriptor.
 *
 * Parameters:
 *     fd: File descriptor to read from.
 * Return value:
 *     String read in (allocated with malloc()), or NULL on error.
 * Notes:
 *     On error, errno is set appropriately.
 */

static char *w32dll_read_asciiz(int fd)
{
    char *str = NULL;
    int size = 0, len = 0;

    do {
        if (len >= size) {
            size = len+100;
            char *newstr = realloc(str, size);
            if (!newstr) {
                int errno_save = errno;
                free(str);
                errno = errno_save;
                return NULL;
            }
            str = newstr;
        }
        if (read(fd, str+len, 1) != 1) {
            free(str);
            errno = ENOEXEC;
            return NULL;
        }
        len++;
    } while (str[len-1] != 0);
    return str;
}

/*************************************************************************/

/**
 * w32dll_init_fs:  Set up the FS segment register to point to a page of
 * data (empty except for the linear address pointer at 0x18).
 *
 * Parameters:
 *     None.
 * Return value:
 *     Nonzero on success, zero on failure.
 * Notes:
 *     On error, errno is set appropriately.
 */

#if defined(OS_LINUX)
# include <asm/unistd.h>
# include <asm/ldt.h>
// This doesn't work, because of PIC:
//static _syscall3(int, modify_ldt, int, func, void *, ptr, unsigned long, bytecount);
static int modify_ldt(int func, void *ptr, unsigned long bytecount) {
    long __res;
    __asm__ volatile ("push %%ebx; mov %%esi, %%ebx; int $0x80; pop %%ebx"
                      : "=a" (__res)
                      : "0" (__NR_modify_ldt), "S" ((long)(func)),
                        "c" ((long)(ptr)), "d" ((long)(bytecount))
                      : "memory");
    /* Errors are from -1 to -128, according to <asm/unistd.h> */
    if ((__res & 0xFFFFFF80UL) == 0xFFFFFF80UL) {
        errno = -__res & 0xFF;
        __res = (unsigned long)-1;
    }
    return (int)__res;
}
#else
# error OS not supported in w32dll_init_fs()
#endif

static int w32dll_init_fs(void)
{
    int fd;
    void *base;
    int segment;
#if defined(OS_LINUX)
    struct user_desc ldt;
#endif

    fd = open("/dev/zero", O_RDWR);
    if (fd < 0)
        return 0;
    base = mmap(NULL, GETPAGESIZE(), PROT_READ | PROT_WRITE,
                MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) {
        int errno_save = errno;
        close(fd);
        errno = errno_save;
        return 0;
    }
    close(fd);
    *(void **)((uint8_t *)base + 0x18) = base;

#if defined(OS_LINUX)
    memset(&ldt, 0, sizeof(ldt)); 
    /* Pick a random number that's hopefully unused.  How does one
     * determine which segment numbers are in use? */
    ldt.entry_number = 172;
    ldt.base_addr = (long)base;
    ldt.limit = GETPAGESIZE();
    ldt.seg_32bit = 1;
    ldt.read_exec_only = 0;
    ldt.seg_not_present = 0;
    ldt.contents = MODIFY_LDT_CONTENTS_DATA;
    ldt.limit_in_pages = 0;
    ldt.seg_not_present = 0;
    ldt.useable = 1;
    if (modify_ldt(17, &ldt, sizeof(ldt)) != 0) {
        int errno_save = errno;
        munmap(base, GETPAGESIZE());
        errno = errno_save;
        return 0;
    }
    segment = ldt.entry_number;
#endif

    /* Bit 2: 1 == use LDT; bits 1-0: 3 == privilege level 3 */
    asm("movw %%ax,%%fs" : : "a" (segment<<3 | 1<<2 | 3));
    return 1;
}

/*************************************************************************/
/*************************************************************************/

#ifdef TEST

#include <stdio.h>

int main(int ac, char **av)
{
    W32DLLHandle dll;

    if (ac < 2 || strcmp(av[1], "-h") == 0 || strcmp(av[1], "--help") == 0) {
        fprintf(stderr, "Usage: %s file.dll [procname | =ordinal]\n", av[0]);
        return -1;
    }
    dll = w32dll_load(av[1], 1);
    if (!dll) {
        perror(av[1]);
        return 1;
    }
    if (ac >= 3) {
        int i;
        void *(*func)(void);
        void ***codec, **functable, ***vid, **vidtable, ***aud, **audtable;
        WINAPI void *(*init)(int, int);
        WINAPI void *(*fini)(void);
        WINAPI void *(*getvid)(void);
        WINAPI void *(*getaud)(void);
        static char buf[0x14000], buf2[720*480*4], buf2a[0x800], buf3[0x2000];
        struct { int dataset; void *workbuf; struct {uint8_t w8, h8, f02, f03; int ofs;} *inparam; void *inbuf; struct {int stride; void *outbuf;} *outparam;} vidparam;
        void *bufptr = buf;
        struct { int flag; char *buffer; } audparam;
        int fd = open("/scratch/pv3/060428-192352-720x480i.dv", O_RDONLY);
        read(fd, buf, 0x14000);
        close(fd);
        if (av[2][0] == '=')
            func = w32dll_lookup_by_ordinal(dll, strtoul(av[2]+1,NULL,0));
        else
            func = w32dll_lookup_by_name(dll, av[2]);
        if (!func) {
            perror(av[2]);
            w32dll_unload(dll);
            return 2;
        }
        printf("%s: %p\n", av[2], func);
        codec = func();
        functable = *codec;
        printf("--> %p [%p %p %p %p...]\n", codec,
               functable[0], functable[1], functable[2], functable[3]);
        init = functable[0];
        fini = functable[1];
        getvid = functable[2];
        getaud = functable[3];
        printf("calling init...\n");
        //(*init)(4, 2);
        asm("push $2; push $4; call *%0" : : "r" (init), "c" (codec));
        printf("...done!\n");
        printf("calling getvid...\n");
        asm("call *%1" : "=a" (vid) : "r" (getvid), "c" (codec));
        vidtable = *vid;
        printf("...done! (%p -> %p %p ... %p ...)\n", aud,
               vidtable[0], vidtable[1], vidtable[5]);
        memset(&vidparam, 0, sizeof(vidparam));
        vidparam.dataset = 0;
        vidparam.workbuf = buf2a;
        vidparam.inparam = calloc(sizeof(*vidparam.inparam), 1);
        vidparam.inparam->w8 = buf[4];
        vidparam.inparam->h8 = buf[5];
        vidparam.inparam->ofs = 0;
        vidparam.inbuf = &bufptr;
        vidparam.outparam = malloc(sizeof(*vidparam.outparam));
        vidparam.outparam->stride = (buf[4]*8)*2;
        vidparam.outparam->outbuf = buf2;
        printf("calling video_decode...\n");  // 10013830
        asm("push %2; call *%1"
            : "=a" (i)
            : "r" (vidtable[5]), "r" (&vidparam), "c" (vid));
        printf("...done! (%d)\n", i);
        printf("and again...\n");
        vidparam.dataset = 1;
        asm("push %2; call *%1"
            : "=a" (i)
            : "r" (vidtable[5]), "r" (&vidparam), "c" (vid));
        printf("...done! (%d)\n", i);
        printf("calling getaud...\n");
        asm("call *%1" : "=a" (aud) : "r" (getaud), "c" (codec));
        audtable = *aud;
        printf("...done! (%p -> %p %p %p ...)\n", aud,
               audtable[0], audtable[1], audtable[2]);
        audparam.flag = 0;
        audparam.buffer = buf;
        *(void **)(buf3+24) = buf3+32;
        printf("calling audio_decode...\n");  // 10013830
        asm("push %3; push %2; call *%1"
            : "=a" (i)
            : "r" (audtable[1]), "r" (&audparam), "r" (buf3), "c" (aud));
        printf("...done! (%d)\n", i);
        fd = open("/scratch/pv3/test.raw", O_WRONLY | O_CREAT | O_TRUNC, 0666);
        write(fd, buf3, sizeof(buf3));
        write(fd, buf2, sizeof(buf2));
        close(fd);
        printf("calling fini...\n");
        asm("call *%0" : : "r" (fini), "c" (codec));
        //(*fini)();
        printf("...done!\n");
    }
    w32dll_unload(dll);
    return 0;
}

#endif

/*************************************************************************/

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
