/*
 * w32dll-emu.c -- Win32 emulation routines to support DLLs
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <wchar.h>

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
#include "w32dll-emu.h"

/*************************************************************************/

/* Define this to enable creation of stubs for unemulated functions that
 * print a warning to stderr and return -1 when the function is called.
 * Note that this leaks one page of mmap() memory plus string memory per
 * stub created.  (Note, however, that for WINAPI functions the debug stub
 * cannot know how many arguments to pop, so the program will probably
 * crash on return.)
 * Defining this also enables some debugging messages to stderr.
 */

//#define W32DLL_EMU_DEBUG

/*************************************************************************/

/* Array of known modules and associated handles (see w32dll-emu.h),
 * terminated with name==NULL. */

#define MOD(modname) {.name = #modname ".dll", .handle = HANDLE_##modname}

static const struct {
    const char *name;
    uint32_t handle;
} emumods[] = {
    MOD(KERNEL32),
    MOD(USER32),
    { .name = NULL }
};

#undef MOD


/* Array of emulated functions, terminated with module==0. */

#define FUNC(mod,func) \
    {.module = HANDLE_##mod, .ordinal = 0, .name = #func, .funcptr = func}

static const struct {
    uint32_t module;  /* handle */
    uint32_t ordinal;
    const char *name;
    void *funcptr;
} emufuncs[] = {
    FUNC(KERNEL32, CloseHandle),
    FUNC(KERNEL32, CreateSemaphoreA),
    FUNC(KERNEL32, CreateSemaphoreW),
    FUNC(KERNEL32, DeleteCriticalSection),
    FUNC(KERNEL32, EnterCriticalSection),
    FUNC(KERNEL32, ExitProcess),
    FUNC(KERNEL32, FreeEnvironmentStringsA),
    FUNC(KERNEL32, FreeEnvironmentStringsW),
    FUNC(KERNEL32, GetACP),
    FUNC(KERNEL32, GetCPInfo),
    FUNC(KERNEL32, GetCommandLineA),
    FUNC(KERNEL32, GetConsoleMode),
    FUNC(KERNEL32, GetCurrentProcessId),
    FUNC(KERNEL32, GetCurrentThreadId),
    FUNC(KERNEL32, GetEnvironmentStringsA),
    FUNC(KERNEL32, GetEnvironmentStringsW),
    FUNC(KERNEL32, GetFileType),
    FUNC(KERNEL32, GetLastError),
    FUNC(KERNEL32, GetModuleFileNameA),
    FUNC(KERNEL32, GetModuleHandleA),
    FUNC(KERNEL32, GetProcAddress),
    FUNC(KERNEL32, GetProcessHeap),
    FUNC(KERNEL32, GetStartupInfoA),
    FUNC(KERNEL32, GetStdHandle),
    FUNC(KERNEL32, GetStringTypeW),
    FUNC(KERNEL32, GetSystemTimeAsFileTime),
    FUNC(KERNEL32, GetTickCount),
    FUNC(KERNEL32, GetVersionExA),
    FUNC(KERNEL32, HeapAlloc),
    FUNC(KERNEL32, HeapCreate),
    FUNC(KERNEL32, HeapDestroy),
    FUNC(KERNEL32, HeapFree),
    FUNC(KERNEL32, HeapReAlloc),
    FUNC(KERNEL32, HeapSize),
    FUNC(KERNEL32, InitializeCriticalSection),
    FUNC(KERNEL32, InterlockedCompareExchange),
    FUNC(KERNEL32, InterlockedCompareExchangePointer),
    FUNC(KERNEL32, InterlockedDecrement),
    FUNC(KERNEL32, InterlockedExchange),
    FUNC(KERNEL32, InterlockedExchangeAdd),
    FUNC(KERNEL32, InterlockedExchangePointer),
    FUNC(KERNEL32, InterlockedIncrement),
    FUNC(KERNEL32, InterlockedTestExchange),
    FUNC(KERNEL32, LCMapStringA),
    FUNC(KERNEL32, LCMapStringW),
    FUNC(KERNEL32, LeaveCriticalSection),
    FUNC(KERNEL32, LoadLibraryA),
    FUNC(KERNEL32, MultiByteToWideChar),
    FUNC(KERNEL32, QueryPerformanceCounter),
    FUNC(KERNEL32, ReleaseSemaphore),
    FUNC(KERNEL32, SetHandleCount),
    FUNC(KERNEL32, SetLastError),
    FUNC(KERNEL32, TlsAlloc),
    FUNC(KERNEL32, TlsFree),
    FUNC(KERNEL32, TlsGetValue),
    FUNC(KERNEL32, TlsSetValue),
    FUNC(KERNEL32, WaitForSingleObject),
    FUNC(KERNEL32, WideCharToMultiByte),
    FUNC(KERNEL32, WriteFile),

    FUNC(USER32, GetActiveWindow),
    FUNC(USER32, MessageBoxA),
    FUNC(USER32, MessageBoxW),

    { .module = 0 }
};

#undef FUNC


/* Debugging functions. */

#if defined(W32DLL_EMU_DEBUG)
# if !defined(MAP_ANONYMOUS)
#  warn MAP_ANONYMOUS not defined, disabling W32DLL_EMU_DEBUG
#  undef W32DLL_EMU_DEBUG
# else
static int32_t debug_stub(const char *module, const char *name,
                          uint32_t ordinal);
static void *create_debug_stub(const char *module, const char *name,
                               uint32_t ordinal);
# endif
#endif

#if defined(W32DLL_EMU_DEBUG)
# define D(x) x
#else
# define D(x) /*nothing*/
#endif

/*************************************************************************/
/*************************************************************************/

/* External interface. */

/*************************************************************************/

/**
 * w32dll_emu_import_by_name, w32dll_emu_import_by_ordinal:  Return the
 * address of the emulated function corresponding to the given import,
 * selected by either name or ordinal.
 *
 * Parameters:
 *      module: Name of the module from which to import.
 *        name: Import name descriptor (w32dll_emu_import_by_name() only).
 *     ordinal: Import ordinal (w32dll_emu_import_by_ordinal() only).
 * Return value:
 *     The address corresponding to the import, or NULL if no emulation
 *     is available for the import.
 */

void *w32dll_emu_import_by_name(const char *module,
                                const struct import_name_entry *name)
{
    int i;
    uint32_t handle = 0;

    for (i = 0; emumods[i].name != NULL; i++) {
        if (strcasecmp(emumods[i].name, module) == 0) {
            handle = emumods[i].handle;
            break;
        }
    }
    if (handle) {
        for (i = 0; emufuncs[i].module != 0; i++) {
            if (emufuncs[i].module == handle
             && strcasecmp(emufuncs[i].name, name->name) == 0)
                return emufuncs[i].funcptr;
        }
    }
#if defined(W32DLL_EMU_DEBUG)
    return create_debug_stub(module, name->name, 0);
#else
    return NULL;
#endif
}

/************************************/

void *w32dll_emu_import_by_ordinal(const char *module, uint32_t ordinal)
{
    /* Not supported yet. */
#if defined(W32DLL_EMU_DEBUG)
    return create_debug_stub(module, NULL, ordinal);
#else
    return NULL;
#endif
}

/*************************************************************************/
/*************************************************************************/

/* The remainder of the file consists of emulation functions. */

/*************************************************************************/
/*************************************************************************/

/* Emulation debugging functions. */

#if defined(W32DLL_EMU_DEBUG)

/*************************************************************************/

/**
 * debug_stub:  Stub function which prints an error message.  Called by
 * machine code generated by create_debug_stub().
 *
 * Parameters:
 *      module: Module name for which the stub is being called.
 *        name: Function name for which the stub is being called, or NULL
 *              if not applicable.
 *     ordinal: Function ordinal for which the stub is being called, if
 *              applicable.
 * Return value:
 *     Always returns -1.
 */

static int32_t debug_stub(const char *module, const char *name,
                          uint32_t ordinal)
{
    char numbuf[16];
    snprintf(numbuf, sizeof(numbuf), "0x%08X", ordinal);
    fprintf(stderr, "[w32dll-emu] Unsupported function: %s/%s\n",
            module, name ? name : numbuf);
    return -1;
}

/*************************************************************************/

/**
 * create_debug_stub:  Create a function which calls debug_stub().
 *
 * Parameters:
 *      module: Module name for which the stub is being created.
 *        name: Function name for which the stub is being created, or NULL
 *              if not applicable.
 *     ordinal: Function ordinal for which the stub is being created, if
 *              applicable.
 * Return value:
 *     A pointer to the newly-created debug stub, or NULL on error.
 */

static void *create_debug_stub(const char *module, const char *name,
                               uint32_t ordinal)
{
    char *funcpage, *s;
    int32_t offset;

    funcpage = mmap(NULL, GETPAGESIZE(), PROT_READ | PROT_WRITE | PROT_EXEC,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (funcpage == MAP_FAILED)
        return NULL;

    if (module)
        module = strdup(module);
    if (!module)
        module = "(null)";
    if (name) {
        name = strdup(name);
        if (!name)
            name = "(null)";
    }

    /* Create the function.  The code below is the equivalent of the
     * following assembly:
     *     pop %eax
     *     pushl ordinal
     *     pushl name
     *     pushl module
     *     push %eax
     *     jmp debug_stub
     */
    strcpy(funcpage, "\x58"
                     "\x68\x11\x11\x11\x11"
                     "\x68\x22\x22\x22\x22"
                     "\x68\x33\x33\x33\x33"
                     "\x50"
                     "\xE9\x44\x44\x44\x44");
    /* Fill in the various values.  Do them in reverse order to avoid
     * strstr() getting confused by intermediate nulls! */
    s = strstr(funcpage,"\x44\x44\x44\x44");
    offset = (int32_t)debug_stub - (int32_t)(s+4);
    memcpy(s, &offset, 4);
    memcpy(strstr(funcpage,"\x33\x33\x33\x33"), &module, 4);
    memcpy(strstr(funcpage,"\x22\x22\x22\x22"), &name, 4);
    memcpy(strstr(funcpage,"\x11\x11\x11\x11"), &ordinal, 4);

    /* Return the new page. */
    return funcpage;
}

#endif  /* W32DLL_EMU_DEBUG */

/*************************************************************************/
/*************************************************************************/

/* Emulated functions in alphabetical order.  See the Windows documentation
 * for details about each function. */

/*************************************************************************/

/* Various data. */

static uint32_t w32_errno = 0;  /* for GetLastError() */
static int tls_alloced[TLS_MINIMUM_AVAILABLE];
static void *tls_data[TLS_MINIMUM_AVAILABLE];

/*************************************************************************/
/*************************************************************************/

/* KERNEL32 functions */

/*************************************************************************/

static WINAPI int CloseHandle(uint32_t handle)
{
    return 1;
}

/*************************************************************************/

static WINAPI uint32_t CreateSemaphoreA(void *attr, uint32_t initial,
                                        uint32_t max, const char *name)
{
    return HANDLE_SEMAPHORE;
}

/*************************************************************************/

static WINAPI uint32_t CreateSemaphoreW(void *attr, uint32_t initial,
                                        uint32_t max, const uint16_t *name)
{
    return HANDLE_SEMAPHORE;
}

/*************************************************************************/

static WINAPI void DeleteCriticalSection(void *lock)
{
    /* Win32 "critical sections" are basically locks shared between threads.
     * We only deal with one thread at the moment, so we ignore all these. */
}

/*************************************************************************/

static WINAPI void EnterCriticalSection(void *lock)
{
}

/*************************************************************************/

static WINAPI void ExitProcess(unsigned int exitcode)
{
    D((fprintf(stderr, "ExitProcess(%u) called, exiting...\n", exitcode)));
    exit(exitcode);
}

/*************************************************************************/

static WINAPI int FreeEnvironmentStringsA(void *env)
{
    return 1;
}

/*************************************************************************/

static WINAPI int FreeEnvironmentStringsW(void *env)
{
    return 1;
}

/*************************************************************************/

static WINAPI unsigned int GetACP(void)
{
    return 0;
}

/*************************************************************************/

static WINAPI int GetCPInfo(unsigned int codepage, CPINFO *result)
{
    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    result->maxbytes = 1;
    result->defchar[0] = '?';
    result->defchar[1] = 0;
    memset(result->leadbytes, 0, sizeof(result->leadbytes));
    return 1;
}

/*************************************************************************/

static WINAPI char *GetCommandLineA(void)
{
    static char dummy_cmdline[] = "dummy.exe";
    return dummy_cmdline;
}

/*************************************************************************/

static WINAPI int GetConsoleMode(uint32_t file, uint16_t *result)
{
    if (file == HANDLE_STDIN) {
        *result = 0x0007;  /* PROCESSED_INPUT | LINE_INPUT | ECHO_INPUT */
    } else if (file == HANDLE_STDOUT) {
        *result = 0x0001;  /* PROCESSED_OUTPUT -- but not really!  oh well */
    } else if (file == HANDLE_STDERR) {
        *result = 0x0000;
    } else {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    return 1;
}

/*************************************************************************/

static WINAPI uint32_t GetCurrentProcessId(void)
{
    return getpid();
}

/*************************************************************************/

static WINAPI uint32_t GetCurrentThreadId(void)
{
    return getpid();
}

/*************************************************************************/

static WINAPI void *GetEnvironmentStringsA(void)
{
    static char dummy_environ[2] = "\0\0";
    return dummy_environ;
}

/*************************************************************************/

static WINAPI void *GetEnvironmentStringsW(void)
{
    static uint16_t dummy_environ[2] = {0,0};
    return dummy_environ;
}

/*************************************************************************/

static WINAPI uint32_t GetFileType(uint32_t file)
{
    SetLastError(NO_ERROR);
    if (file==HANDLE_STDIN || file==HANDLE_STDOUT || file==HANDLE_STDERR)
        return 2;  /* FILE_TYPE_CHAR */
    SetLastError(ERROR_INVALID_HANDLE);
    return 0;  /* FILE_TYPE_UNKNOWN */
}

/*************************************************************************/

static WINAPI uint32_t GetLastError(void)
{
    return w32_errno;
}

/*************************************************************************/

static WINAPI uint32_t GetModuleFileNameA(uint32_t module, char *buf,
                                          uint32_t size)
{
    int n = -1;

    if (module == 0 && module == HANDLE_DEFAULT) {
        n = snprintf(buf, size, "%s", "dummy.exe");
    } else {
        int i;
        for (i = 0; emumods[i].name != NULL; i++) {
            if (emumods[i].handle == module) {
                n = snprintf(buf, size, "%s", emumods[i].name);
                break;
            }
        }
        if (!emumods[i].name) {
            SetLastError(ERROR_INVALID_HANDLE);
            return 0;
        }
    }
    if (n < 0 || n >= size) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    return n;
}

/*************************************************************************/

static WINAPI uint32_t GetModuleHandleA(const char *name)
{
    int i;

    if (!name)
        return HANDLE_DEFAULT;
    for (i = 0; emumods[i].name != NULL; i++) {
        if (strcasecmp(emumods[i].name, name) == 0)
            return emumods[i].handle;
    }
    D((fprintf(stderr, "GetModuleHandleA(%s) -> 0\n", name)));
    SetLastError(ERROR_FILE_NOT_FOUND);
    return 0;
}

/*************************************************************************/

static WINAPI FARPROC GetProcAddress(uint32_t module, const char *name)
{
    int i;

    if (!name) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    if (module == 0 && module == HANDLE_DEFAULT) {
        D((fprintf(stderr, "GetProcAddress(DEFAULT, %s) -> NULL\n", name)));
        SetLastError(ERROR_INVALID_FUNCTION);
        return NULL;
    }
    for (i = 0; emufuncs[i].module != 0; i++) {
        if (emufuncs[i].module==module && strcasecmp(emufuncs[i].name,name)==0)
            return emufuncs[i].funcptr;
    }
    D((fprintf(stderr, "GetProcAddress(%d, %s) -> NULL\n", module, name)));
    SetLastError(ERROR_INVALID_HANDLE);
    return NULL;
}

/*************************************************************************/

static WINAPI uint32_t GetProcessHeap(void)
{
    return HANDLE_HEAP;
}

/*************************************************************************/

static WINAPI void GetStartupInfoA(STARTUPINFO *result)
{
    result->size = sizeof(*result);
    result->reserved = NULL;
    result->desktop = NULL;
    result->title = "dummy";
    result->x = 0;
    result->y = 0;
    result->w = 640;
    result->h = 480;
    result->wchars = 80;
    result->hchars = 30;
    result->fill = 0;
    result->flags = 0;
    result->show = 1;
    result->reserved2 = 0;
    result->reserved3 = NULL;
    result->h_stdin = HANDLE_STDIN;
    result->h_stdout = HANDLE_STDOUT;
    result->h_stderr = HANDLE_STDERR;
}

/*************************************************************************/

static WINAPI uint32_t GetStdHandle(uint32_t index)
{
    if (index == (uint32_t)-10)
        return HANDLE_STDIN;
    if (index == (uint32_t)-11)
        return HANDLE_STDOUT;
    if (index == (uint32_t)-12)
        return HANDLE_STDERR;
    SetLastError(ERROR_INVALID_PARAMETER);
    return INVALID_HANDLE_VALUE;
}

/*************************************************************************/

static WINAPI int GetStringTypeW(uint32_t type, const uint16_t *str,
                                 int len, uint16_t *typebuf)
{
    int i;

    if (!str || len <= 0 || !typebuf) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    for (i = 0; i < len; i++) {
        switch (type) {
          case 1:
            typebuf[i] = 0;
            if (str[i] <= 0x7F) {
                if (isupper((char)str[i]))
                    typebuf[i] |= 0x0001;
                if (islower((char)str[i]))
                    typebuf[i] |= 0x0002;
                if (isdigit((char)str[i]))
                    typebuf[i] |= 0x0004;
                if (isspace((char)str[i]))
                    typebuf[i] |= 0x0008;
                if (ispunct((char)str[i]))
                    typebuf[i] |= 0x0010;
                if (iscntrl((char)str[i]))
                    typebuf[i] |= 0x0020;
                if (isxdigit((char)str[i]))
                    typebuf[i] |= 0x0080;
                if (isalpha((char)str[i]))
                    typebuf[i] |= 0x0100;
            }  // if (str[i] <= 0x7F)
            break;
          case 2:
            if (str[i] >= 0x20 && str[i] <= 0x7E) {
                typebuf[i] = 1;
            } else {
                typebuf[i] = 0;
            }
            break;
          case 3:
            if (isalpha((char)str[i])) {
                typebuf[i] = 0x8040;  /* ALPHA | HALFWIDTH */
            }
            break;
        }
    }
    return 1;
}

/*************************************************************************/

static WINAPI void GetSystemTimeAsFileTime(uint64_t *result)
{
    /* Time is in 100-nanosecond units since 1601/1/1 0:00 UTC */
    /* Difference from 1601/1/1 to 1970/1/1 = 369 years, incl. 89 leap years */
    *result = ((uint64_t)time(NULL) + (uint64_t)(369*365+89)*86400) * 10000000;
}

/*************************************************************************/

static WINAPI uint32_t GetTickCount(void)
{
    struct timeval tv;
#if defined(HAVE_GETTIMEOFDAY)
    gettimeofday(&tv, NULL);
#else
    tv.tv_sec = time(NULL);
    tv.tv_usec = 0;
#endif
    return tv.tv_sec*1000 + tv.tv_usec/1000;  // as good as anything else
}

/*************************************************************************/

static WINAPI int GetVersionExA(OSVERSIONINFOEX *result)
{
    if (!result || result->size < 148) {  // 148: sizeof(OSVERSIONINFO)
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    /* Emulate Windows 2000 */
    result->major = 5;
    result->minor = 0;
    result->build = 31337;
    result->platform = 2;
    memset(result->extra, 0, sizeof(result->extra));
    if (result->size >= sizeof(*result)) {
        result->sp_major = 4;
        result->sp_minor = 0;
        result->suite = 0x0000;
        result->type = 0x01;
        result->reserved = 0;
    }
    return 1;
}

/*************************************************************************/

#define HEAPALLOC_MAGIC 0x9D1A9DA1
#define HEAPFREE_MAGIC (~HEAPALLOC_MAGIC)

static WINAPI void *HeapAlloc(uint32_t heap, uint32_t flags, size_t size)
{
    void *ptr = malloc(size+8);
    D((fprintf(stderr, "HeapAlloc(%u) -> %p%s", size, ptr, ptr ? "" : "\n")));
    if (!ptr) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    ptr = (uint32_t *)ptr+2;
    ((uint32_t *)ptr)[-2] = HEAPALLOC_MAGIC;
    ((uint32_t *)ptr)[-1] = size;
    D((fprintf(stderr, " -> %p\n", ptr)));
    if (flags & 0x00000008)  /* HEAP_ZERO_MEMORY */
        memset(ptr, 0, size);
    return ptr;
}

/*************************************************************************/

static WINAPI uint32_t HeapCreate(uint32_t flags, size_t initial, size_t max)
{
    /* Just share the same "heap" */
    return HANDLE_HEAP;
}

/*************************************************************************/

static WINAPI int HeapDestroy(uint32_t heap)
{
    return 1;  /* Ignore */
}

/*************************************************************************/

static WINAPI int HeapFree(uint32_t heap, uint32_t flags, void *ptr)
{
    D((fprintf(stderr, "HeapFree(%p) [%08X %u]\n", ptr,
               ptr ? ((uint32_t *)ptr)[-2] : 0,
               ptr ? ((uint32_t *)ptr)[-1] : 0)));
    if (ptr) {
        if (((uint32_t *)ptr)[-2] != HEAPALLOC_MAGIC) {
            D((fprintf(stderr, "HeapFree() on %s pointer %p!\n",
                       ((uint32_t *)ptr)[-2]==HEAPFREE_MAGIC ? "freed" : "bad",
                       ptr)));
            SetLastError(ERROR_INVALID_PARAMETER);
            return 0;
        }
        ((uint32_t *)ptr)[-1] = HEAPFREE_MAGIC;
        free((uint32_t *)ptr-2);
    }
    return 1;
}

/*************************************************************************/

static WINAPI void *HeapReAlloc(uint32_t heap, uint32_t flags, void *ptr,
                                size_t size)
{
    size_t oldsize;

    D((fprintf(stderr, "HeapReAlloc(%p,%u) -> ", ptr, size)));
    if (!ptr)
        return HeapAlloc(heap, flags, size);
    if (((uint32_t *)ptr)[-2] != HEAPALLOC_MAGIC) {
        D((fprintf(stderr, "HeapReAlloc() on %s pointer %p!\n",
                   ((uint32_t *)ptr)[-2]==HEAPFREE_MAGIC ? "freed" : "bad",
                   ptr)));
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    oldsize = ((uint32_t *)ptr)[-1];
    ptr = realloc((uint32_t *)ptr-2, size+8);
    D((fprintf(stderr, "oldsize %u -> %p%s", oldsize, ptr, ptr ? "" : "\n")));
    if (!ptr) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return NULL;
    }
    ptr = (uint32_t *)ptr+2;
    ((uint32_t *)ptr)[-2] = HEAPALLOC_MAGIC;
    ((uint32_t *)ptr)[-1] = size;
    D((fprintf(stderr, " -> %p\n", ptr)));
    if (size > oldsize && (flags & 0x00000008))  /* HEAP_ZERO_MEMORY */
        memset((uint8_t *)ptr + oldsize, 0, size - oldsize);
    return ptr;
}

/*************************************************************************/

static WINAPI uint32_t HeapSize(uint32_t heap, uint32_t flags, const void *ptr)
{
    D((fprintf(stderr, "HeapSize(%p) -> %u\n", ptr,
               (uint32_t) (((const uint64_t *)ptr)[-1]))));
    return (uint32_t) (((const uint64_t *)ptr)[-1]);
}

/*************************************************************************/

static WINAPI void InitializeCriticalSection(void *lock)
{
}

/*************************************************************************/

static WINAPI int32_t InterlockedCompareExchange
                            (int32_t *var, int32_t testval, int32_t newval)
{
    int32_t oldval = *var;
    if (oldval == testval)
        *var = newval;
    return oldval;
}

/*************************************************************************/

static WINAPI void *InterlockedCompareExchangePointer
                                 (void ***var, void *testval, void *newval)
{
    void *oldval = *var;
    if (oldval == testval)
        *var = newval;
    return oldval;
}

/*************************************************************************/

static WINAPI int32_t InterlockedDecrement(int32_t *var)
{
    return --*var;
}

/*************************************************************************/

static WINAPI int32_t InterlockedExchange(int32_t *var, int32_t newval)
{
    int32_t oldval = *var;
    *var = newval;
    return oldval;
}

/*************************************************************************/

static WINAPI int32_t InterlockedExchangeAdd(int32_t *var, int32_t addval)
{
    int32_t oldval = *var;
    *var += addval;
    return oldval;
}

/*************************************************************************/

static WINAPI void *InterlockedExchangePointer(void **var, void *newval)
{
    void *oldval = *var;
    *var = newval;
    return oldval;
}

/*************************************************************************/

static WINAPI int32_t InterlockedIncrement(int32_t *var)
{
    return ++*var;
}

/*************************************************************************/

static WINAPI int32_t InterlockedTestExchange(int32_t *var, int32_t testval,
                                              int32_t newval)
{
    int32_t oldval = *var;
    if (oldval == testval)
        *var = newval;
    return oldval;
}

/*************************************************************************/

static WINAPI int LCMapStringA
        (uint32_t locale, uint32_t flags, const char *in, int inlen,
         char *out, int outsize)
{
    if (!in || !inlen || outsize < 0 || (outsize > 0 && !out)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (inlen < 0)
        inlen = strlen(in) + 1;  // include terminating null
    if (outsize == 0)
        return inlen;
    if (outsize < inlen) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(out, in, inlen);
    return inlen;
}

/*************************************************************************/

static WINAPI int LCMapStringW
        (uint32_t locale, uint32_t flags, const uint16_t *in, int inlen,
         uint16_t *out, int outsize)
{
    if (!in || !inlen || outsize < 0 || (outsize > 0 && !out)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (inlen < 0) {
        inlen = 0;
        while (in[inlen])
            inlen++;
        inlen++;  // include null terminator
    }
    if (outsize == 0)
        return inlen;
    if (outsize < inlen) {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return 0;
    }
    memcpy(out, in, inlen*2);
    return inlen;
}

/*************************************************************************/

static WINAPI void LeaveCriticalSection(void *lock)
{
}

/*************************************************************************/

static WINAPI uint32_t LoadLibraryA(char *filename)
{
    return GetModuleHandleA(filename);
}

/*************************************************************************/

static WINAPI int MultiByteToWideChar
        (unsigned int codepage, uint32_t flags, const unsigned char *in,
         int inlen, uint16_t *out, int outsize)
{
    int i, outlen;

    if (!in || !inlen || outsize < 0 || (outsize > 0 && !out)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (inlen < 0) {
        inlen = 0;
        while (in[inlen])
            inlen++;
        inlen++;  // include null terminator
    }
    outlen = 0;

    for (i = 0; i < inlen; i++) {
        if (out) {
            if (outlen >= outsize) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return 0;
            }
            *out++ = in[i];
        }
        outlen++;
    }

    return outlen;
}

/*************************************************************************/

static WINAPI uint32_t QueryPerformanceCounter(int64_t *result)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    if (!result) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    *result = (int64_t)tv.tv_sec*1000000 + tv.tv_usec;  // sure, why not?
    return 1;
}

/*************************************************************************/

static WINAPI int ReleaseSemaphore(uint32_t sem, int32_t release_count,
                                   int32_t *previous)
{
    if (sem != HANDLE_SEMAPHORE) {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (previous)
        *previous = 0;
    return 1;
}

/*************************************************************************/

static WINAPI void SetHandleCount(uint32_t count)
{
    /* obsolete Win16 function, does nothing */
}

/*************************************************************************/

static WINAPI void SetLastError(uint32_t error)
{
    w32_errno = error;
}

/*************************************************************************/

static WINAPI uint32_t TlsAlloc(void)
{
    int i;
    for (i = 0; i < TLS_MINIMUM_AVAILABLE; i++) {
        if (!tls_alloced[i]) {
            tls_alloced[i] = 1;
            D((fprintf(stderr, "TlsAlloc() succeeded with %d\n", i)));
            return i;
        }
    }
    D((fprintf(stderr, "TlsAlloc() failed\n")));
    return ~0;
}

/*************************************************************************/

static WINAPI int TlsFree(uint32_t index)
{
    if (index >= TLS_MINIMUM_AVAILABLE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    tls_alloced[index] = 0;
    return 1;
}

/*************************************************************************/

static WINAPI void *TlsGetValue(uint32_t index)
{
    if (index >= TLS_MINIMUM_AVAILABLE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }
    SetLastError(NO_ERROR);
    return tls_data[index];
}

/*************************************************************************/

static WINAPI int TlsSetValue(uint32_t index, void *value)
{
    if (index >= TLS_MINIMUM_AVAILABLE) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    tls_data[index] = value;
    return 1;
}

/*************************************************************************/

static WINAPI uint32_t WaitForSingleObject(uint32_t handle, uint32_t msec)
{
    return 0;  /* or WAIT_TIMEOUT == 0x102 */
}

/*************************************************************************/

static WINAPI int WideCharToMultiByte
        (unsigned int codepage, uint32_t flags, const uint16_t *in, int inlen,
         char *out, int outsize, const char *defchar, int *defchar_used)
{
    int i, outlen;

    if (!in || !inlen || outsize < 0 || (outsize > 0 && !out)) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (inlen < 0) {
        inlen = 0;
        while (in[inlen])
            inlen++;
        inlen++;  // include null terminator
    }
    if (!defchar)
        defchar = "?";
    outlen = 0;

    /* Simple implementation (FIXME look into glibc's conversion functions?) */
    for (i = 0; i < inlen; i++) {
        if (in[i] <= 0x7F) {
            if (out) {
                if (outlen >= outsize) {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return 0;
                }
                *out++ = (char)in[i];
            }
            outlen++;
        } else {
            if (out) {
                const char *s;
                if (outlen + strlen(defchar) > outsize) {
                    SetLastError(ERROR_INSUFFICIENT_BUFFER);
                    return 0;
                }
                s = defchar;
                while (*s)
                    *out++ = *s++;
            }
            outlen += strlen(defchar);
            if (defchar_used)
                *defchar_used = 1;
        }
    }

    return outlen;
}

/*************************************************************************/

static WINAPI int WriteFile(uint32_t file, const void *buf, uint32_t len,
                            uint32_t *written, void *overlapped)
{
    int fd = -1, nwritten;

    if (!buf) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    if (file == HANDLE_STDIN) {
        SetLastError(ERROR_ACCESS_DENIED);
        return 0;
    } else if (file == HANDLE_STDOUT) {
        D((fd = 1));
    } else if (file == HANDLE_STDERR) {
        D((fd = 2));
    } else {
        SetLastError(ERROR_INVALID_HANDLE);
        return 0;
    }
    if (len == 0) {
        if (written)
            *written = 0;
        return 1;
    }
    if (fd < 0) {
        /* Suppress stdout/stderr output in non-debug mode */
        if (written)
            *written = len;
        return 1;
    }

    do {
        errno = 0;
        nwritten = write(fd, buf, len);
    } while (nwritten <= 0 && errno == EINTR);
    if (nwritten <= 0) {
        if (errno == EBADF || errno == EINVAL)
            SetLastError(ERROR_ACCESS_DENIED);
        else if (errno == EFAULT)
            SetLastError(ERROR_INVALID_ACCESS);
        else if (errno == EPIPE)
            SetLastError(ERROR_BROKEN_PIPE);
        else if (errno == EAGAIN)
            SetLastError(ERROR_IO_PENDING);
        else if (errno == ENOSPC || errno == EFBIG)
            SetLastError(ERROR_DISK_FULL);
        else if (errno == EIO)
            SetLastError(ERROR_WRITE_FAULT);
        else
            SetLastError(ERROR_UNKNOWN);
        return 0;
    }

    if (written)
        *written = (uint32_t)nwritten;
    return 1;
}

/*************************************************************************/
/*************************************************************************/

/* USER32 functions. */

/*************************************************************************/

static WINAPI uint32_t GetActiveWindow(void)
{
    return HANDLE_WINDOW;
}

/*************************************************************************/

#define OUT(str) do {                           \
    const char *__s = str;                      \
    do {                                        \
        int __len, __lpad, __rpad;              \
        const char *__t = __s + strlen(__s);    \
        if (__t - __s > maxline)                \
            __t = __s + maxline;                \
        __len = __t - __s;                      \
        __lpad = (maxline - __len) / 2;         \
        __rpad = maxline - __len - __lpad;      \
        fprintf(stderr, "|%*s%.*s%*s|\n", __lpad, "", __len, __s, __rpad, "");\
        __s = __t;                              \
    } while (*__s);                             \
} while (0)
#define MAXLINEWIDTH 77

static WINAPI int MessageBoxA(uint32_t window, const char *text,
                              const char *title, unsigned int type)
{
    char *mytext, *s;
    int maxline, i;
    char dashbuf[MAXLINEWIDTH+1];

    if (!text || !title) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return 0;
    }
    mytext = strdup(text);  // make sure we can strtok() it
    if (!mytext) {
        SetLastError(ERROR_OUTOFMEMORY);
        return 0;
    }

    maxline = strlen(title);
    for (s = mytext; *s; s += strspn(s, "\r\n")) {
        char *t = s + strcspn(s, "\r\n");
        if (t-s > maxline)
            maxline = t-s;
        s = t;
    }
    if (maxline > MAXLINEWIDTH)
        maxline = MAXLINEWIDTH;
    for (i = 0; i < maxline; i++)
        dashbuf[i] = '-';
    dashbuf[maxline] = 0;

    fprintf(stderr, "+%s+\n", dashbuf);
    OUT(title);
    fprintf(stderr, "+%s+\n", dashbuf);
    for (s = strtok(mytext,"\r\n"); s; s = strtok(NULL,"\r\n"))
        OUT(s);
    fprintf(stderr, "+%s+\n", dashbuf);

    free(mytext);
    return 1;
}

#undef OUT
#undef MAXLINEWIDTH

/*************************************************************************/

static WINAPI int MessageBoxW(uint32_t window, const uint16_t *text,
                              const uint16_t *title, unsigned int type)
{
    char textbuf[10000], titlebuf[1000];
    if (!WideCharToMultiByte(0, 0, text, 0, textbuf, sizeof(textbuf), 0, 0))
        strcpy(textbuf, "<<buffer overflow>>");
    if (!WideCharToMultiByte(0, 0, title, 0, titlebuf, sizeof(titlebuf), 0, 0))
        strcpy(titlebuf, "<<buffer overflow>>");
    return MessageBoxA(window, textbuf, titlebuf, type);;
}

/*************************************************************************/
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
