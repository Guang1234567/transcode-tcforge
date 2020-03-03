/*
 * accore.c -- core aclib functions
 * Written by Andrew Church <achurch@achurch.org>
 *
 * This file is part of transcode, a video stream processing tool.
 * transcode is free software, distributable under the terms of the GNU
 * General Public License (version 2 or later).  See the file COPYING
 * for details.
 */

#include "ac.h"
#include "ac_internal.h"
#include "imgconvert.h"

#include <stdio.h>
#include <string.h>

#if defined(ARCH_X86) || defined(ARCH_X86_64)
static int cpuinfo_x86(void);
#endif

/*************************************************************************/

/* Library initialization function.  Determines CPU features, then calls
 * all initialization subfunctions with appropriate flags.  Returns 1 on
 * success, 0 on failure.  This function can be called multiple times to
 * change the set of acceleration features to be used. */

int ac_init(int accel)
{
    accel &= ac_cpuinfo();
    if (!ac_average_init(accel)
     || !ac_imgconvert_init(accel)
     || !ac_memcpy_init(accel)
     || !ac_rescale_init(accel)
    ) {
        return 0;
    }
    return 1;
}

/*************************************************************************/

/* Returns the set of acceleration features supported by this CPU. */

int ac_cpuinfo(void)
{
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    return cpuinfo_x86();
#else
    return 0;
#endif
}

/*************************************************************************/

/* Returns the endianness of this CPU (AC_BIG_ENDIAN or AC_LITTLE_ENDIAN). */

int ac_endian(void)
{
    volatile int test;

    test = 1;
    if (*((uint8_t *)&test))
        return AC_LITTLE_ENDIAN;
    else
        return AC_BIG_ENDIAN;
}

/*************************************************************************/

/* Utility routine to convert a set of flags to a descriptive string.  The
 * string is stored in a static buffer overwritten each call.  `filter'
 * selects whether to filter out flags not supported by the CPU. */

const char *ac_flagstotext(int accel)
{
    static char retbuf[1000];
    if (!accel)
        return "none";
    snprintf(retbuf, sizeof(retbuf), "%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
             accel & AC_SSE5                  ? " sse5"     : "",
             accel & AC_SSE4A                 ? " sse4a"    : "",
             accel & AC_SSE42                 ? " sse42"    : "",
             accel & AC_SSE41                 ? " sse41"    : "",
             accel & AC_SSSE3                 ? " ssse3"    : "",
             accel & AC_SSE3                  ? " sse3"     : "",
             accel & AC_SSE2                  ? " sse2"     : "",
             accel & AC_SSE                   ? " sse"      : "",
             accel & AC_3DNOWEXT              ? " 3dnowext" : "",
             accel & AC_3DNOW                 ? " 3dnow"    : "",
             accel & AC_MMXEXT                ? " mmxext"   : "",
             accel & AC_MMX                   ? " mmx"      : "",
             accel & AC_CMOVE                 ? " cmove"    : "",
             accel & (AC_IA32ASM|AC_AMD64ASM) ? " asm"      : "");
    return *retbuf ? retbuf+1 : retbuf;  /* skip initial space */
}

/* Utility routine to parse a comma-separate descriptive string to the
   corrisponding flag. The reverse of ac_flagstotext.
   Returns 1 on success, 0 on failure */

#define AC_FLAG_LEN     16

int ac_parseflags(const char *text, int *accel)
{
    int parsed = 1, done = 0;
    if (!text || !accel)
        return 0;
#if defined(ARCH_X86) || defined(ARCH_X86_64)
    *accel = 0;

    while (parsed && !done) {
        char buf[AC_FLAG_LEN + 1] = { '\0' };
        const char *comma = strchr(text, ',');
        if (!comma) {
            strncpy(buf, text, AC_FLAG_LEN);
            done = 1;
        } else {
            /* parse the remaining and exit*/
            size_t len = (comma - text);
            if (len > AC_FLAG_LEN)
                len = AC_FLAG_LEN;
            strncpy(buf, text, len);
        }
//fprintf(stderr, "(%s) buf=[%s]\n", __func__, buf);
        if (strcasecmp(buf, "C") == 0)  // dummy for "no accel"
            *accel |= 0;
#ifdef ARCH_X86
        else if (strcasecmp(buf, "asm"     ) == 0)
            *accel |= AC_IA32ASM;
#endif
#ifdef ARCH_X86_64
        else if (strcasecmp(buf, "asm"     ) == 0)
            *accel |= AC_AMD64ASM;
#endif
        else if (strcasecmp(buf, "mmx"     ) == 0)
            *accel |= AC_MMX;
        else if (strcasecmp(buf, "mmxext"  ) == 0)
            *accel |= AC_MMXEXT;
        else if (strcasecmp(buf, "3dnow"   ) == 0)
            *accel |= AC_3DNOW;
        else if (strcasecmp(buf, "3dnowext") == 0)
            *accel |= AC_3DNOWEXT;
        else if (strcasecmp(buf, "sse"     ) == 0)
            *accel |= AC_SSE;
        else if (strcasecmp(buf, "sse2"    ) == 0)
            *accel |= AC_SSE2;
        else if (strcasecmp(buf, "sse3"    ) == 0)
            *accel |= AC_SSE3;
        else if (strcasecmp(buf, "ssse3"   ) == 0)
            *accel |= AC_SSSE3;
        else if (strcasecmp(buf, "sse41"   ) == 0)
            *accel |= AC_SSE41;
        else if (strcasecmp(buf, "sse42"   ) == 0)
            *accel |= AC_SSE42;
        else if (strcasecmp(buf, "sse4a"   ) == 0)
            *accel |= AC_SSE4A;
        else if (strcasecmp(buf, "sse5"    ) == 0)
            *accel |= AC_SSE5;
        else
            parsed = 0;
        text = comma + 1;
    }
#endif
    return parsed;
}

#undef AC_FLAG_LEN

/*************************************************************************/
/*************************************************************************/

/* Private functions to return acceleration flags corresponding to available
 * CPU features for various CPUs.  Currently only x86 is supported. */

/*************************************************************************/

#if defined(ARCH_X86) || defined(ARCH_X86_64)

#ifdef ARCH_X86_64
# define EAX "%%rax"
# define EBX "%%rbx"
# define ESI "%%rsi"
# define PUSHF "pushfq"
# define POPF "popfq"
#else
# define EAX "%%eax"
# define EBX "%%ebx"
# define ESI "%%esi"
# define PUSHF "pushfl"
# define POPF "popfl"
#endif

/* Macro to execute the CPUID instruction with EAX = func.  Results are
 * placed in ret_a (EAX), ret_b (EBX), ret_c (ECX), and ret_d (EDX), which
 * must be lvalues.  Note that we save and restore EBX (RBX on x86-64)
 * because it is the PIC register. */
#define CPUID(func,ret_a,ret_b,ret_c,ret_d)                             \
    asm("mov "EBX", "ESI"; cpuid; xchg "EBX", "ESI                      \
        : "=a" (ret_a), "=S" (ret_b), "=c" (ret_c), "=d" (ret_d)        \
        : "a" (func))

/* Various CPUID flags.  The second word of the macro name indicates the
 * function (1: function 1, X1: function 0x80000001) and register (D: EDX)
 * to which the value belongs. */
#define CPUID_1D_CMOVE          (1UL<<15)
#define CPUID_1D_MMX            (1UL<<23)
#define CPUID_1D_SSE            (1UL<<25)
#define CPUID_1D_SSE2           (1UL<<26)
#define CPUID_1C_SSE3           (1UL<< 0)
#define CPUID_1C_SSSE3          (1UL<< 9)
#define CPUID_1C_SSE41          (1UL<<19)
#define CPUID_1C_SSE42          (1UL<<20)
#define CPUID_X1D_AMD_MMXEXT    (1UL<<22)  /* AMD only */
#define CPUID_X1D_AMD_3DNOW     (1UL<<31)  /* AMD only */
#define CPUID_X1D_AMD_3DNOWEXT  (1UL<<30)  /* AMD only */
#define CPUID_X1D_CYRIX_MMXEXT  (1UL<<24)  /* Cyrix only */
#define CPUID_X1C_AMD_SSE4A     (1UL<< 6)  /* AMD only */
#define CPUID_X1C_AMD_SSE5      (1UL<<11)  /* AMD only */

static int cpuinfo_x86(void)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t cpuid_max, cpuid_ext_max;  /* Maximum CPUID function numbers */
    union {
        char string[13];
        struct { uint32_t ebx, edx, ecx; } regs;
    } cpu_vendor;  /* 12-byte CPU vendor string + trailing null */
    uint32_t cpuid_1D, cpuid_1C, cpuid_X1C, cpuid_X1D;
    int accel;

    /* First see if the CPUID instruction is even available.  We try to
     * toggle bit 21 (ID) of the flags register; if the bit changes, then
     * CPUID is available. */
    asm(PUSHF"                  \n\
        pop "EAX"               \n\
        mov %%eax, %%edx        \n\
        xor $0x200000, %%eax    \n\
        push "EAX"              \n\
        "POPF"                  \n\
        "PUSHF"                 \n\
        pop "EAX"               \n\
        xor %%edx, %%eax"
        : "=a" (eax) : : "edx");
    if (!eax)
        return 0;

    /* Determine the maximum function number available, and save the vendor
     * string */
    CPUID(0, cpuid_max, ebx, ecx, edx);
    cpu_vendor.regs.ebx = ebx;
    cpu_vendor.regs.ecx = ecx;
    cpu_vendor.regs.edx = edx;
    cpu_vendor.string[12] = 0;
    cpuid_ext_max = 0;  /* FIXME: how do early CPUs respond to 0x80000000? */
    CPUID(0x80000000, cpuid_ext_max, ebx, ecx, edx);

    /* Read available features */
    cpuid_1D = cpuid_1C = cpuid_X1C = cpuid_X1D = 0;
    if (cpuid_max >= 1)
        CPUID(1, eax, ebx, cpuid_1C, cpuid_1D);
    if (cpuid_ext_max >= 0x80000001)
        CPUID(0x80000001, eax, ebx, cpuid_X1C, cpuid_X1D);

    /* Convert to acceleration flags */
#ifdef ARCH_X86_64
    accel = AC_AMD64ASM;  /* but not IA32! (register size issues) */
#else
    accel = AC_IA32ASM;
#endif
    if (cpuid_1D & CPUID_1D_CMOVE)
        accel |= AC_CMOVE;
    if (cpuid_1D & CPUID_1D_MMX)
        accel |= AC_MMX;
    if (cpuid_1D & CPUID_1D_SSE)
        accel |= AC_SSE;
    if (cpuid_1D & CPUID_1D_SSE2)
        accel |= AC_SSE2;
    if (cpuid_1C & CPUID_1C_SSE3)
        accel |= AC_SSE3;
    if (cpuid_1C & CPUID_1C_SSSE3)
        accel |= AC_SSSE3;
    if (cpuid_1C & CPUID_1C_SSE41)
        accel |= AC_SSE41;
    if (cpuid_1C & CPUID_1C_SSE42)
        accel |= AC_SSE42;
    if (strcmp(cpu_vendor.string, "AuthenticAMD") == 0) {
        if (cpuid_X1D & CPUID_X1D_AMD_MMXEXT)
            accel |= AC_MMXEXT;
        if (cpuid_X1D & CPUID_X1D_AMD_3DNOW)
            accel |= AC_3DNOW;
        if (cpuid_X1D & CPUID_X1D_AMD_3DNOWEXT)
            accel |= AC_3DNOWEXT;
        if (cpuid_X1C & CPUID_X1C_AMD_SSE4A)
            accel |= AC_SSE4A;
        if (cpuid_X1C & CPUID_X1C_AMD_SSE5)
            accel |= AC_SSE5;
    } else if (strcmp(cpu_vendor.string, "CyrixInstead") == 0) {
        if (cpuid_X1D & CPUID_X1D_CYRIX_MMXEXT)
            accel |= AC_MMXEXT;
    }

    /* And return */
    return accel;
}

#endif  /* ARCH_X86 || ARCH_X86_64 */

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
