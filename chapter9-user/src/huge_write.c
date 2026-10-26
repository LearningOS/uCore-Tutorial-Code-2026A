#include "user.h"

int main(void)
{
    unsigned char buffer[1024];
    for (usize i = 0; i < sizeof(buffer); ++i) buffer[i] = (unsigned char)i;
    int fd = u_open("testf", O_CREATE | O_WRONLY);
    CHECK(fd > 0, "open test file failed");
    uint64 start = u_time_ms();
    for (unsigned i = 0; i < 1024; ++i)
        CHECK(u_write(fd, buffer, sizeof(buffer)) == (long)sizeof(buffer), "write test file failed");
    CHECK(u_close(fd) == 0, "close test file failed");
    uint64 elapsed_ms = u_time_ms() - start;
    /* A zero-duration sample is possible on a very fast clock/emulator. */
    uint64 speed = elapsed_ms ? (1UL << 20) / elapsed_ms : 0;
    u_printf("1MiB written, time cost = %lums, write speed = %luKiB/s\n", elapsed_ms, speed);
    return 0;
}
