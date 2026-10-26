#ifndef CHAPTER9_USER_H
#define CHAPTER9_USER_H

typedef unsigned long usize;
typedef unsigned long uint64;
#define NULL ((void *)0)
#define O_RDONLY 0x000
#define O_WRONLY 0x001
#define O_RDWR   0x002
#define O_CREATE 0x200
#define O_TRUNC  0x400
#define FRAMEBUFFER_ADDRESS 0x10000000UL
#define VIRTGPU_XRES 1280
#define VIRTGPU_YRES 800
#define VIRTGPU_LEN ((usize)VIRTGPU_XRES * VIRTGPU_YRES * 4)

long u_read(int fd, void *buf, usize len);
long u_write(int fd, const void *buf, usize len);
int u_open(const char *path, int flags);
int u_close(int fd);
int u_fork(void);
int u_exec(const char *path, char *const argv[]);
int u_wait(int pid, int *exit_code);
void u_yield(void);
__attribute__((noreturn)) void u_exit(int status);
uint64 u_time_ms(void);
unsigned char *u_framebuffer(void);
int u_framebuffer_flush(void);
void u_puts(const char *s);
void u_putnum(uint64 n);
void u_printf(const char *format, ...);
__attribute__((noreturn)) void u_fail(const char *message);
usize strlen(const char *s);
int strcmp(const char *a, const char *b);
int memcmp(const void *a, const void *b, usize n);
void *memset(void *dst, int c, usize n);
void *memcpy(void *dst, const void *src, usize n);
void *memmove(void *dst, const void *src, usize n);
#define CHECK(condition, message) do { if (!(condition)) u_fail(message); } while (0)
#endif
