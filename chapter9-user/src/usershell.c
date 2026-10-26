#include "user.h"

int main(void)
{
    char command[128];
    usize used = 0;
    u_puts("uCore user shell\n>> ");
    for (;;) {
        char ch;
        if (u_read(0, &ch, 1) != 1) continue;
        if (ch == '\r' || ch == '\n') {
            u_puts("\n");
            command[used] = 0;
            if (used) {
                int pid = u_fork();
                CHECK(pid >= 0, "fork failed");
                if (!pid) {
                    char *argv[] = {command, NULL};
                    u_exec(command, argv);
                    u_puts("Command not found: "); u_puts(command); u_puts("\n");
                    u_exit(127);
                }
                int status = -1;
                CHECK(u_wait(pid, &status) == pid, "wait failed");
                if (status) u_printf("Process exited with code %d\n", status);
            }
            used = 0;
            u_puts(">> ");
        } else if (ch == 8 || ch == 127) {
            if (used) { --used; u_puts("\b \b"); }
        } else if (ch >= 32 && ch < 127 && used + 1 < sizeof(command)) {
            command[used++] = ch;
            u_write(1, &ch, 1);
        }
    }
}
