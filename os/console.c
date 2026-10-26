#include "console.h"
#include "uart.h"

void consputc(int c)
{
	uart_write(c);
}

void console_init()
{
	uart_init();
}

int consgetc()
{
	return uart_read();
}
