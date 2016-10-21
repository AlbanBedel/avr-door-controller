#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <avr/interrupt.h>
#include <avr/io.h>

#include "uart.h"
#include "completion.h"
#include "gpio.h"

#ifndef BAUD_TOL
#  define BAUD_TOL 5
#endif

struct uart {
	uint8_t direction;

	uart_on_recv_t on_recv;
	void *on_recv_context;

	uart_on_sent_t on_sent;
	void *on_sent_context;

	const uint8_t *tx_data;
	uint8_t tx_size;
	uint8_t tx_pos;
};

static struct uart uart;

static int8_t uart_set_mode(uint32_t baud, uint8_t stop_bits, uint8_t parity)
{
	static const uint32_t f_cpu_100 = 100 * (F_CPU);
	static const uint32_t f_cpu = F_CPU;
	uint32_t baud_min = (100 - (BAUD_TOL)) * baud;
	uint32_t baud_max = (100 + (BAUD_TOL)) * baud;
	uint32_t ubrr_value;
	uint8_t use_2x;

	if (stop_bits < 1 || stop_bits > 2)
		return -EINVAL;

	ubrr_value = (f_cpu + 8UL * baud) / (16UL * baud) - 1UL;
	if ((f_cpu_100 > (16 * (ubrr_value + 1)) * baud_max) ||
	    (f_cpu_100 < (16 * (ubrr_value + 1)) * baud_min))
		use_2x = 1;
	else
		use_2x = 0;

	if (use_2x) {
		ubrr_value = (f_cpu + 4UL * baud) / (8UL * baud) - 1UL;
		if ((f_cpu_100 > (8 * (ubrr_value + 1)) * baud_max) ||
		    (f_cpu_100 < (8 * (ubrr_value + 1)) * baud_min))
			return -EINVAL;
	}

	UBRR0 = ubrr_value;
	if (use_2x)
		UCSR0A |= _BV(U2X0);
	else
		UCSR0A &= ~_BV(U2X0);

	UCSR0C = (parity << UPM00) | ((stop_bits - 1) << USBS0) |
		_BV(UCSZ00) | _BV(UCSZ01); /* 8 bits */

	return 0;
}

int8_t uart_init(uint8_t direction, uint32_t rate,
	       uint8_t stop_bits, uint8_t parity)
{
	int8_t err;

	if (!direction) // Disable ?
		return -EINVAL;

	err = uart_set_mode(rate, stop_bits, parity);
	if (err)
		return err;

	uart.direction = direction;

	if (uart.direction & UART_DIRECTION_TX) {
		gpio_direction_output(UART_TX_GPIO, 1);
		UCSR0B |= _BV(TXEN0);
	}

	if (uart.direction & UART_DIRECTION_RX) {
		gpio_direction_input(UART_RX_GPIO, 1);
		UCSR0B |= _BV(RXEN0);
	}

	return 0;
}

int8_t uart_set_recv_handler(uart_on_recv_t on_recv, void *context)
{
	if (!(uart.direction & UART_DIRECTION_RX))
		return -EINVAL;

	/* Disable the RX IRQ */
	UCSR0B &= ~_BV(RXCIE0);

	/* Update the callback */
	uart.on_recv = on_recv;
	uart.on_recv_context = context;

	/* Enable the RX IRQ if needed */
	if (uart.on_recv)
		UCSR0B |= _BV(RXCIE0);

	return 0;
}

int8_t uart_send(const void *data, uint8_t size,
		 uart_on_sent_t on_sent, void *context)
{
	if (!(uart.direction & UART_DIRECTION_TX))
		return -EINVAL;

	/* Check if there is a transfer underway */
	if (uart.tx_size > 0)
		return -EBUSY;

	/* Handle 0 length strings */
	if (size == 0) {
		if (uart.on_sent)
			uart.on_sent(uart.on_sent_context);
		return 0;
	}

	uart.tx_data = data;
	uart.tx_size = size;
	uart.on_sent = on_sent;
	uart.on_sent_context = context;

	/* Enable the load IRQ */
	UCSR0B |= _BV(UDRIE0);
	return 0;
}

int8_t uart_blocking_send(const void *data, uint8_t size)
{
	struct completion cmp = {};
	int8_t err;

	err = uart_send(data, size, completion_done_cb, &cmp);
	if (err)
		return err;

	completion_wait(&cmp);
	return 0;
}

int8_t uart_write(const char *str, uart_on_sent_t on_sent, void *context)
{
	return uart_send(str, strlen(str), on_sent, context);
}

int8_t uart_blocking_write(const char *str)
{
	struct completion cmp = {};
	int8_t err;

	err = uart_write(str, completion_done_cb, &cmp);
	if (err)
		return err;

	completion_wait(&cmp);
	return 0;
}

ISR(USART_RX_vect)
{
	uint8_t byte = UDR0;

	if (uart.on_recv)
		uart.on_recv(byte, uart.on_recv_context);
	else
		UCSR0B &= ~_BV(RXCIE0);
}

ISR(USART_UDRE_vect)
{
	if (uart.tx_pos < uart.tx_size) {
		UDR0 = uart.tx_data[uart.tx_pos];
		uart.tx_pos++;
	}

	if (uart.tx_pos >= uart.tx_size) {
		UCSR0B &= ~_BV(UDRIE0);
		uart.tx_data = NULL;
		uart.tx_size = 0;
		uart.tx_pos = 0;

		if (uart.on_sent)
			uart.on_sent(uart.on_sent_context);
	}
}
