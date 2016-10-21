#ifndef UART_H
#define UART_H

#include <stdint.h>

typedef void (*uart_on_recv_t)(uint8_t byte, void *context);

typedef void (*uart_on_sent_t)(void *context);


#define UART_DIRECTION_RX	1
#define UART_DIRECTION_TX	2
#define UART_DIRECTION_BOTH	((UART_DIRECTION_RX) | (UART_DIRECTION_TX))

#define UART_PARITY_NONE	0
#define UART_PARITY_EVEN	2
#define UART_PARITY_ODD		3

int8_t uart_init(uint8_t direction, uint32_t rate,
		 uint8_t stop_bits, uint8_t parity);

int8_t uart_set_recv_handler(uart_on_recv_t on_recv, void *context);

int8_t uart_send(const void *data, uint8_t size,
		 uart_on_sent_t on_sent, void *context);

int8_t uart_blocking_send(const void *data, uint8_t size);

int8_t uart_write(const char *str, uart_on_sent_t on_sent, void *context);

int8_t uart_blocking_write(const char *str);

#endif /* UART_H */
