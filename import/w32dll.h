/*
 * w32dll.h -- w32dll.c interface declaration
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#ifndef W32DLL_H
#define W32DLL_H

/*************************************************************************/

/* DLL handle type (opaque) */
struct w32dllhandle_;
typedef struct w32dllhandle_ *W32DLLHandle;

/* WINAPI and CALLBACK calling format definitions, used by some DLLs. */
#if defined(__GNUC__)
# define WINAPI __attribute__((stdcall))
#else
# define WINAPI __stdcall
#endif
#define CALLBACK WINAPI

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
extern W32DLLHandle w32dll_load(const char *path, int compat);

/**
 * w32dll_unload:  Unload the given DLL from memory.  Does nothing if the
 * given handle is zero.
 *
 * Parameters:
 *     dll: DLL handle.
 * Return value:
 *     None.
 */
extern void w32dll_unload(W32DLLHandle dll);

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
extern void *w32dll_lookup_by_name(W32DLLHandle dll, const char *name);

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
extern void *w32dll_lookup_by_ordinal(W32DLLHandle dll, uint32_t ordinal);

/*************************************************************************/

#endif  /* W32DLL_H */

/*
 * Local variables:
 *   c-file-style: "stroustrup"
 *   c-file-offsets: ((case-label . *) (statement-case-intro . *))
 *   indent-tabs-mode: nil
 * End:
 *
 * vim: expandtab shiftwidth=4:
 */
