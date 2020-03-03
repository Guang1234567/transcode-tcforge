/*
 * w32dll-emu.h -- w32dll.c internal header file for Win32 emulation
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef W32DLL_EMU_H
#define W32DLL_EMU_H

/*************************************************************************/

/* Function pointer return types. (Note that on the whole, we avoid using
 * Windows types in favor of standard ANSI types like uint32_t.) */
typedef CALLBACK long (*FARPROC)(void);

/*************************************************************************/

/* Various constants. */

/* Local handle constants.  HANDLE_DEFAULT (for the DLL itself) is defined
 * in w32dll-local.h. */
#define HANDLE_KERNEL32                 2
#define HANDLE_USER32                   3
#define HANDLE_WINDOW                   101
#define HANDLE_HEAP                     201
#define HANDLE_STDIN                    301
#define HANDLE_STDOUT                   302
#define HANDLE_STDERR                   303
#define HANDLE_SEMAPHORE                401
#define HANDLE_MAXLOCAL                 4095

#define INVALID_HANDLE_VALUE            (~0)

#define TLS_MINIMUM_AVAILABLE           64

#define ERROR_UNKNOWN                   99999
#define NO_ERROR                        0
#define ERROR_INVALID_FUNCTION          1
#define ERROR_FILE_NOT_FOUND            2
#define ERROR_ACCESS_DENIED             5
#define ERROR_INVALID_HANDLE            6
#define ERROR_NOT_ENOUGH_MEMORY         8
#define ERROR_INVALID_ACCESS            12
#define ERROR_OUTOFMEMORY               14
#define ERROR_WRITE_FAULT               29
#define ERROR_INVALID_PARAMETER         87
#define ERROR_BROKEN_PIPE               109
#define ERROR_DISK_FULL                 112
#define ERROR_INSUFFICIENT_BUFFER       122
#define ERROR_IO_PENDING                997

/*************************************************************************/

/* Various structures. */

typedef struct {
    unsigned int maxbytes;
    uint8_t defchar[2];
    uint8_t leadbytes[12];
} CPINFO;

typedef struct {
    uint32_t size;
    uint32_t major;     // set to 5
    uint32_t minor;     // set to 0 (5.0: Windows 2000)
    uint32_t build;     // can be anything
    uint32_t platform;  // set to 2 (VER_PLATFORM_WIN32_NT)
    char extra[128];
    uint16_t sp_major;  // set to 4 (W2k SP4)
    uint16_t sp_minor;  // set to 0
    uint16_t suite;     // set to 0
    uint8_t type;       // set to 0x01 (VER_NT_WORKSTATION)
    uint8_t reserved;
} OSVERSIONINFOEX;

typedef struct {
    uint32_t size;
    char *reserved;
    char *desktop;
    char *title;
    uint32_t x, y, w, h, wchars, hchars, fill;
    uint32_t flags;
    uint16_t show;
    uint16_t reserved2;
    uint8_t *reserved3;
    uint32_t h_stdin;
    uint32_t h_stdout;
    uint32_t h_stderr;
} STARTUPINFO;

/*************************************************************************/

/* Emulated function prototypes (these are all static within w32dll-emu.c). */

/**** KERNEL32.dll ****/

static WINAPI int      CloseHandle(uint32_t handle);
static WINAPI uint32_t CreateSemaphoreA(void *attr, uint32_t initial,
                                        uint32_t max, const char *name);
static WINAPI uint32_t CreateSemaphoreW(void *attr, uint32_t initial,
                                        uint32_t max, const uint16_t *name);
static WINAPI void     DeleteCriticalSection(void *lock);
static WINAPI void     EnterCriticalSection(void *lock);
static WINAPI void     ExitProcess(unsigned int exitcode);
static WINAPI int      FreeEnvironmentStringsA(void *env);
static WINAPI int      FreeEnvironmentStringsW(void *env);
static WINAPI unsigned int
                       GetACP(void);
static WINAPI int      GetCPInfo(unsigned int codepage, CPINFO *result);
static WINAPI char *   GetCommandLineA(void);
static WINAPI int      GetConsoleMode(uint32_t file, uint16_t *result);
static WINAPI uint32_t GetCurrentProcessId(void);
static WINAPI uint32_t GetCurrentThreadId(void);
static WINAPI void *   GetEnvironmentStringsA(void);
static WINAPI void *   GetEnvironmentStringsW(void);
static WINAPI uint32_t GetFileType(uint32_t file);
static WINAPI uint32_t GetLastError(void);
static WINAPI uint32_t GetModuleFileNameA(uint32_t module, char *buf,
                                          uint32_t size);
static WINAPI uint32_t GetModuleHandleA(const char *name);
static WINAPI FARPROC  GetProcAddress(uint32_t handle, const char *name);
static WINAPI uint32_t GetProcessHeap(void);
static WINAPI void     GetStartupInfoA(STARTUPINFO *result);
static WINAPI uint32_t GetStdHandle(uint32_t index);
static WINAPI int      GetStringTypeW(uint32_t type, const uint16_t *str,
                                      int len, uint16_t *typebuf);
static WINAPI void     GetSystemTimeAsFileTime(uint64_t *result);
static WINAPI uint32_t GetTickCount(void);
static WINAPI int      GetVersionExA(OSVERSIONINFOEX *result);
static WINAPI void *   HeapAlloc(uint32_t heap, uint32_t flags, size_t size);
static WINAPI uint32_t HeapCreate(uint32_t flags, size_t initial, size_t max);
static WINAPI int      HeapDestroy(uint32_t heap);
static WINAPI int      HeapFree(uint32_t heap, uint32_t flags, void *ptr);
static WINAPI void *   HeapReAlloc(uint32_t heap, uint32_t flags, void *ptr,
                                   size_t size);
static WINAPI uint32_t HeapSize(uint32_t heap, uint32_t flags, const void*ptr);
static WINAPI int32_t  InterlockedCompareExchange
                            (int32_t *var, int32_t testval, int32_t newval);
static WINAPI void *   InterlockedCompareExchangePointer
                                 (void ***var, void *testval, void *newval);
static WINAPI int32_t  InterlockedDecrement(int32_t *var);
static WINAPI int32_t  InterlockedExchange(int32_t *var, int32_t newval);
static WINAPI int32_t  InterlockedExchangeAdd(int32_t *var, int32_t addval);
static WINAPI void *   InterlockedExchangePointer(void **var, void *newval);
static WINAPI int32_t  InterlockedIncrement(int32_t *var);
static WINAPI int32_t  InterlockedTestExchange(int32_t *var, int32_t testval,
                                               int32_t newval);
static WINAPI void     InitializeCriticalSection(void *lock);
static WINAPI int LCMapStringA
        (uint32_t locale, uint32_t flags, const char *in, int inlen,
         char *out, int outsize);
static WINAPI int LCMapStringW
        (uint32_t locale, uint32_t flags, const uint16_t *in, int inlen,
         uint16_t *out, int outsize);
static WINAPI void     LeaveCriticalSection(void *lock);
static WINAPI uint32_t LoadLibraryA(char *filename);
static WINAPI int      MultiByteToWideChar
        (unsigned int codepage, uint32_t flags, const unsigned char *in,
         int inlen, uint16_t *out, int outsize);
static WINAPI uint32_t QueryPerformanceCounter(int64_t *result);
static WINAPI int      ReleaseSemaphore(uint32_t sem, int32_t release_count,
                                        int32_t *previous);
static WINAPI void     SetHandleCount(uint32_t count);
static WINAPI void     SetLastError(uint32_t error);
static WINAPI uint32_t TlsAlloc(void);
static WINAPI int      TlsFree(uint32_t index);
static WINAPI void *   TlsGetValue(uint32_t index);
static WINAPI int      TlsSetValue(uint32_t index, void *value);
static WINAPI uint32_t WaitForSingleObject(uint32_t handle, uint32_t msec);
static WINAPI int      WideCharToMultiByte
        (unsigned int codepage, uint32_t flags, const uint16_t *in, int inlen,
         char *out, int outsize, const char *defchar, int *defchar_used);
static WINAPI int      WriteFile(uint32_t file, const void *buf, uint32_t len,
                                 uint32_t *written, void *overlapped);

/**** USER32.dll ****/

static WINAPI uint32_t GetActiveWindow(void);
static WINAPI int      MessageBoxA(uint32_t window, const char *text,
                                   const char *title, unsigned int type);
static WINAPI int      MessageBoxW(uint32_t window, const uint16_t *text,
                                   const uint16_t *title, unsigned int type);

/*************************************************************************/

#endif  /* W32DLL_EMU_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
