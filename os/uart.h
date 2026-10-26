#ifndef UART_H
#define UART_H

void uart_init(void);
void uart_write(int c);
unsigned char uart_read(void);
void uart_handle_irq(void);
int uart_buffer_empty(void);

#endif
