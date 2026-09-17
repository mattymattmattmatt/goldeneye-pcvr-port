#include "str.h"

#ifdef PORT
/* D150: strcpy/strncpy/strcat are __nonnull__ builtins to GCC, so a plain
 * `if (src == NULL)` on the parameter is optimised away as provably-dead.
 * Launder the pointer through an empty asm so the compiler cannot prove it
 * non-NULL and the guard survives (-Og elided it otherwise — verified in
 * the disassembly). */
static inline const void *ge_launder_ptr(const void *p) {
    __asm__("" : "+r"(p));
    return p;
}
#define GE_IS_NULL(p) (ge_launder_ptr(p) == NULL)
#endif

char *strcpy(char *dst, const char *src) {
    unsigned char *ptr = dst;
#ifdef PORT
    /* D150: the watch briefing/objective pages assemble their text with
     * strcpy/strcat(buf, langGet(id)); langGet() returns NULL for a string
     * bank the PC menu flow never loaded (D129/D143), so a NULL src here
     * faults. Treat NULL as the empty string (blank text, not a crash) —
     * same philosophy as the D143 textRender/textMeasure NULL guards.
     * N64 langGet never returns NULL for these ids, so this is inert there. */
    if (GE_IS_NULL(src)) { if (!GE_IS_NULL(dst)) *ptr = '\0'; return dst; }
#endif
    while(*ptr++ = *src++);
    return dst;
}

char *strncpy(char *dst, const char *src, size_t n) {
    unsigned char *ptr = dst;
#ifdef PORT
    if (GE_IS_NULL(src)) { while (n--) *ptr++ = '\0'; return dst; }  /* D150 */
#endif
    while((*ptr++ = *src++)) { 
        if (--n == 0) {
            break;
        }
    }
    while(n--) {
        (*ptr++) = '\0';
    }
    return dst;
}

char *strcat(char *dst, const char *src) {
    unsigned char *ptr = dst;
#ifdef PORT
    if (GE_IS_NULL(dst)) { return dst; }          /* D150 */
    if (GE_IS_NULL(src)) { return dst; }          /* D150 — see strcpy note */
#endif
    while (*ptr) { ptr++; };
    while(*ptr++ = *src++);
    return dst;
}

int strcmp(const char* str1, const char* str2) {
    unsigned int var;
    unsigned char c1;
    unsigned char c2;
    while (TRUE) {
        var = c1 = *(str1++);
        if (var != (c2 = *str2)) {
            if (c1 < c2) {
                return -1;
            } else {
                return 1;
            }
        }
        if (c1 == '\0') {
            return 0;
        }
        str2++;
    }
}

int strncmp(const char *str1, const char *str2, size_t n) {
    unsigned int var;
    unsigned char c1;
    unsigned char c2;
    while (TRUE) {
        if (n == 0) {
            return 0;
        }
        n--;
        var = c1 = *str1++;
        if (var != (c2 = *str2)) {
            if (c1 < c2) {
                return -1;
            } else {
                return 1;
            }
        }
        if (c1 == '\0') {
            return 0;
        }
        str2++;
    }
}


#ifdef PORT
/*
 * The four ctype functions below are file-local in the port build.
 *
 * They are the N64 SDK's own, decompiled, and on Windows they collide at link
 * time with the C runtime's: recent MinGW-w64 force-defines isspace and
 * friends in a single libmsvcrt member, so whenever the linker pulls that
 * member in for anything else, the definitions here become a duplicate --
 * "multiple definition of `isspace'". Older toolchains pulled the members more
 * lazily and never hit it, which is why this only appears on a current MSYS2.
 *
 * Nothing outside this file calls them. strtol below is the only caller;
 * xprintf.c has its own isdigit macro. So internal linkage removes the clash
 * without changing a single call or its behaviour, and the game keeps its own
 * definitions rather than silently inheriting the CRT's -- which differ: the
 * SDK's isspace does not count '\r' as space.
 *
 * strcmp, strncmp and strtol cannot take the same treatment -- they have real
 * callers across the game -- and do not currently collide.
 */
#define GE_CTYPE_LINKAGE static
#else
#define GE_CTYPE_LINKAGE
#endif

GE_CTYPE_LINKAGE unsigned char toupper(unsigned char c) {
    if ((c >= 'a') && (c <= 'z')) {
        return ('A' + c - 'a');
    } else {
        return c;
    }
}

GE_CTYPE_LINKAGE int isdigit(unsigned char c) {
    return ((c >= '0') && (c <= '9'));
}

GE_CTYPE_LINKAGE int isalpha(unsigned char c) {
    return (((c >= 'a') && (c <= 'z')) || 
            ((c >= 'A') && (c <= 'Z')));
}

GE_CTYPE_LINKAGE int isspace(unsigned char c) {
    return ((c == ' ') || (c == '\t') || (c == '\n') || (c == '\f') || (c == '\v'));
}

#define	ULONG_MAX ((unsigned long)(~0L)) /* 0xFFFFFFFF */
long int strtol(const char *str, char **endptr, int base) {
    int neg;
    unsigned char *ptr;
    unsigned int cutoff;
    unsigned int cutlim;
    unsigned int accum;
    unsigned char c;
    unsigned char *before;
    int overflow;
    if ((base < 0) || (base == 1) || (base > 36)) {
        base = 10;
    }
    ptr = str;
    while (isspace(*ptr)) { ptr++; };
    if ((int)*ptr) {
        if (*ptr == '-') {
            neg = 1;
            ptr++;
        } else if (*ptr == '+') {
            neg = 0;
            ptr++;
        } else {
            neg = 0;
        }
        if (base == 16) {
            if ((ptr[0] == '0') && (toupper(ptr[1]) == 'X')) {
                ptr += 2;
            }
        }
        if (base == 0) {
            if (ptr[0] == '0') {
                if (toupper(ptr[1]) == 'X') {
                    ptr += 2;
                    base = 16;
                } else {                    
                    base = 8;
                }
            } else {
                base = 10;
            }
        }
        before = ptr;
        overflow = 0;
        accum = 0;
        cutoff = ULONG_MAX / base;
        cutlim = ULONG_MAX % base;
        for (; (int)(c = *ptr); ptr++) {
            if (isdigit(c)) {
                c -= '0';
            } else if (isalpha(c)) {
                c = (toupper(c) - ('A' - 0xA));
            } else {
                break;
            }
            if (c >= base) {
                break;
            }
            if ((accum > cutoff) || ((accum == cutoff) && ((unsigned int)c > cutlim))) {
                overflow = 1;
            } else {
                accum *= base;
                accum += (unsigned int)c;
            }
        }
        if (ptr != before) {
            if (endptr != NULL) {
                *endptr = ptr;
            }
            if (overflow) {
                return -1;
            }
            return (neg ? -accum : accum);
        }
    }
    if (endptr != NULL) {
        *endptr = str;
    }
    return 0;
}
