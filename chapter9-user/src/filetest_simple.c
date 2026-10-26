#include "user.h"

int main(void)
{
    const char *test_str = "Hello, world!";
    int fd = u_open("filea", O_CREATE | O_WRONLY);
    CHECK(fd > 0, "open filea for write");
    CHECK(u_write(fd, test_str, strlen(test_str)) == (long)strlen(test_str), "write filea");
    CHECK(u_close(fd) == 0, "close filea after write");
    fd = u_open("filea", O_RDONLY);
    CHECK(fd > 0, "open filea for read");
    char buffer[100] = {0};
    long len = u_read(fd, buffer, sizeof(buffer));
    CHECK(u_close(fd) == 0, "close filea after read");
    CHECK(len == (long)strlen(test_str), "filea read length");
    CHECK(memcmp(buffer, test_str, len) == 0, "filea readback differs");
    u_puts("file_test passed!\n");
    return 0;
}
