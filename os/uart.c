/* NS16550A UART: S-mode owns receive interrupts; transmit remains polling. */
#include "uart.h"
#include "defs.h"
#include "io.h"
#include "plic.h"

#define UART_REG(offset) (*(volatile unsigned char *)(UART0 + (offset)))
#define RHR 0
#define THR 0
#define IER 1
#define FCR 2
#define LCR 3
#define MCR 4
#define LSR 5
#define LSR_DATA_READY 1
#define LSR_TX_EMPTY (1 << 5)
#define RX_CAPACITY 4096

static struct {
	unsigned char bytes[RX_CAPACITY];
	uint head, tail, count;
	struct io_cond available;
} uart;

void uart_init(void)
{
	irq_state_t state = io_irq_save();
	io_cond_init(&uart.available);
	uart.head = uart.tail = uart.count = 0;
	UART_REG(IER) = 0;
	UART_REG(LCR) = 3; /* eight data bits, one stop bit, no parity, DLAB clear */
	UART_REG(FCR) = 7; /* enable FIFO and discard pre-initialization bytes */
	UART_REG(MCR) = 0x0b; /* DTR, RTS, OUT2 */
	UART_REG(IER) = 1; /* receive-data interrupt only */
	io_irq_restore(state);
}

/* Provided raw register operation; -1 means no byte is currently available. */
static int __attribute__((unused)) uart_raw_read(void)
{
	if (UART_REG(LSR) & LSR_DATA_READY)
		return UART_REG(RHR);
	return -1;
}

void uart_write(int c)
{
	irq_state_t state = io_irq_save();
	while (!(UART_REG(LSR) & LSR_TX_EMPTY))
		;
	UART_REG(THR) = (unsigned char)c;
	io_irq_restore(state);
}

int uart_buffer_empty(void)
{
	irq_state_t state = io_irq_save();
	int empty = uart.count == 0;
	io_irq_restore(state);
	return empty;
}

unsigned char uart_read(void)
{
	/* LAB9_BEGIN uart_read */
	panic("TODO(LAB9): uart_read");
	/* LAB9_END uart_read */
}

void uart_handle_irq(void)
{
	/* LAB9_BEGIN uart_handle_irq */
	panic("TODO(LAB9): uart_handle_irq");
	/* LAB9_END uart_handle_irq */
}
