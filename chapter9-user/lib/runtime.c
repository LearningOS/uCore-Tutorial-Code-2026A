#include "user.h"
#include <stdarg.h>

/* This is the existing C uCore syscall ABI: open(path,flags), exec(path,argv).
   The framebuffer extension keeps the chapter's 2000/2001 syscall numbers. */
static long call3(long id, long x, long y, long z)
{
    register long a0 __asm__("a0") = x;
    register long a1 __asm__("a1") = y;
    register long a2 __asm__("a2") = z;
    register long a7 __asm__("a7") = id;
    __asm__ volatile("ecall" : "+r"(a0) : "r"(a1), "r"(a2), "r"(a7) : "memory");
    return a0;
}
long u_read(int fd, void *buf, usize len) { return call3(63, fd, (long)buf, len); }
long u_write(int fd, const void *buf, usize len) { return call3(64, fd, (long)buf, len); }
int u_open(const char *path, int flags) { return call3(56, (long)path, flags, 0); }
int u_close(int fd) { return call3(57, fd, 0, 0); }
int u_fork(void) { return call3(220, 0, 0, 0); }
int u_exec(const char *path, char *const argv[]) { return call3(221, (long)path, (long)argv, 0); }
int u_wait(int pid, int *status) { return call3(260, pid, (long)status, 0); }
void u_yield(void) { call3(124, 0, 0, 0); }
void u_exit(int status) { call3(93, status, 0, 0); for (;;) { } }
uint64 u_time_ms(void)
{
    struct { uint64 sec, usec; } tv;
    CHECK(call3(169, (long)&tv, 0, 0) == 0, "gettimeofday failed");
    return tv.sec * 1000 + tv.usec / 1000;
}
unsigned char *u_framebuffer(void) { return (unsigned char *)call3(2000, 0, 0, 0); }
int u_framebuffer_flush(void) { return call3(2001, 0, 0, 0); }
void u_puts(const char *s) { u_write(1, s, strlen(s)); }
void u_putnum(uint64 n)
{
    char digits[21];
    unsigned int count = 0;
    do { digits[count++] = '0' + n % 10; n /= 10; } while (n);
    while (count) u_write(1, &digits[--count], 1);
}
void u_printf(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    while (*format) {
        if (*format != '%') { u_write(1, format++, 1); continue; }
        ++format;
        int wide = 0;
        if (*format == 'l') { wide = 1; ++format; }
        if (*format == 's') u_puts(va_arg(args, const char *));
        else if (*format == 'u') u_putnum(wide ? va_arg(args, uint64) : va_arg(args, unsigned));
        else if (*format == 'd') {
            long v = wide ? va_arg(args, long) : va_arg(args, int);
            if (v < 0) { u_puts("-"); u_putnum((uint64)(-(v + 1)) + 1); }
            else u_putnum(v);
        } else if (*format == '%') u_puts("%");
        else u_puts("?");
        if (*format) ++format;
    }
    va_end(args);
}
void u_fail(const char *message) { u_puts("FAIL: "); u_puts(message); u_puts("\n"); u_exit(1); }
usize strlen(const char *s) { usize n = 0; while (s[n]) ++n; return n; }
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return (unsigned char)*a - (unsigned char)*b;
}
int memcmp(const void *a, const void *b, usize n)
{
    const unsigned char *x = a, *y = b;
    for (usize i = 0; i < n; ++i) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}
void *memset(void *dst, int c, usize n)
{
    unsigned char *p = dst;
    for (usize i = 0; i < n; ++i) p[i] = c;
    return dst;
}
void *memcpy(void *dst, const void *src, usize n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    for (usize i = 0; i < n; ++i) d[i] = s[i];
    return dst;
}
void *memmove(void *dst, const void *src, usize n)
{
    unsigned char *d = dst;
    const unsigned char *s = src;
    if (d < s) return memcpy(dst, src, n);
    while (n) { --n; d[n] = s[n]; }
    return dst;
}
