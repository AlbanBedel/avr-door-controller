/*
 * Copyright (C) 2017 Alban Bedel <albeu@free.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <stdlib.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include <libubox/ulog.h>
#include <libubox/list.h>

#include "avr-door-controller-daemon.h"

/* Message decoder state */
#define AVR_DOOR_CTRL_INIT			0
#define AVR_DOOR_CTRL_SYNC			1
#define AVR_DOOR_CTRL_RECV_TYPE			2
#define AVR_DOOR_CTRL_RECV_LENGTH		3
#define AVR_DOOR_CTRL_RECV_PAYLOAD		4
#define AVR_DOOR_CTRL_RECV_CRC			5

#define UART_CTRL_START				0x7E
#define UART_CTRL_ESC				0x7D
#define UART_CTRL_UNESCAPE(x)			((x) ^ 0x20)
#define UART_CTRL_ESCAPE(x)			UART_CTRL_UNESCAPE(x)
#define UART_CTRL_CRC_INIT			0

#define UART_CTRL_BUFFER_SIZE (1 + (sizeof(struct avr_door_ctrl_msg) + 2) * 2)


struct avr_door_ctrl_uart_transport {
	struct avr_door_ctrl_transport transport;

	uint8_t recv_buffer[UART_CTRL_BUFFER_SIZE];
	unsigned int recv_buffer_len;
	unsigned int recv_buffer_pos;

	unsigned int recv_state;
	bool recv_escape;
	unsigned int recv_msg_pos;
	uint16_t recv_crc;

	uint8_t send_buffer[UART_CTRL_BUFFER_SIZE];
	unsigned int send_buffer_len;
	unsigned int send_buffer_pos;
};


static uint16_t crc_update(uint16_t crc, uint8_t data)
{
	int i;

	crc = crc ^ ((uint16_t)data << 8);
	for (i=0; i < 8; i++) {
		if (crc & 0x8000)
			crc = (crc << 1) ^ 0x1021;
		else
			crc <<= 1;
        }

	return crc;
}

static uint16_t msg_compute_crc(
	const struct avr_door_ctrl_msg *msg)
{
	uint16_t crc = UART_CTRL_CRC_INIT;
	int i;

	crc = crc_update(crc, msg->type);
	crc = crc_update(crc, msg->length);
	for (i = 0; i < msg->length; i++)
		crc = crc_update(crc, msg->payload[i]);

	return crc;
}

static int msg_encode_byte(uint8_t *buffer, uint8_t byte)
{
	if (byte == UART_CTRL_START || byte == UART_CTRL_ESC) {
		buffer[0] = UART_CTRL_ESC;
		buffer[1] = UART_CTRL_ESCAPE(byte);
		return 2;
	} else {
		buffer[0] = byte;
		return 1;
	}
}

static int msg_encode(const struct avr_door_ctrl_msg *msg, uint8_t *buffer)
{
	uint16_t crc = UART_CTRL_CRC_INIT;
	int i, pos = 0;

	buffer[pos++] = UART_CTRL_START;

	pos += msg_encode_byte(buffer + pos, msg->type);
	pos += msg_encode_byte(buffer + pos, msg->length);
	for (i = 0; i < msg->length; i++)
		pos += msg_encode_byte(buffer + pos, msg->payload[i]);

	crc = msg_compute_crc(msg);
	pos += msg_encode_byte(buffer + pos, crc);
	pos += msg_encode_byte(buffer + pos, crc >> 8);

	return pos;
}

static int uart_ctrl_transport_recv_byte(
	struct avr_door_ctrl_uart_transport *uart,
	uint8_t byte, struct avr_door_ctrl_msg *msg)
{

	/* During init just wait for the welcome message */
	if (uart->recv_state == AVR_DOOR_CTRL_INIT) {
		if (byte == '\n')
			uart->recv_state = AVR_DOOR_CTRL_SYNC;
		return -ENODATA;
	}

	if (byte == UART_CTRL_START) {
		/* Warn the upper layer if there was a transport error */
		int err = uart->recv_state == AVR_DOOR_CTRL_SYNC ?
			-ENODATA : -EPROTO;

		uart->recv_state = AVR_DOOR_CTRL_RECV_TYPE;
		uart->recv_escape = false;
		return err;
	}

	/* While not in sync ignore everything */
	if (uart->recv_state == AVR_DOOR_CTRL_SYNC)
		return -ENODATA;

	/* Unescape */
	if (uart->recv_escape) {
		byte = UART_CTRL_UNESCAPE(byte);
		uart->recv_escape = false;
	} else if (byte == UART_CTRL_ESC) {
		uart->recv_escape = true;
		return -ENODATA;
	}

	switch (uart->recv_state) {
	case AVR_DOOR_CTRL_RECV_TYPE:
		msg->type = byte;
		uart->recv_state = AVR_DOOR_CTRL_RECV_LENGTH;
		return -ENODATA;

	case AVR_DOOR_CTRL_RECV_LENGTH:
		msg->length = byte;
		if (msg->length > 0)
			uart->recv_state = AVR_DOOR_CTRL_RECV_PAYLOAD;
		else
			uart->recv_state = AVR_DOOR_CTRL_RECV_CRC;
		uart->recv_msg_pos = 0;
		uart->recv_crc = 0;
		return -ENODATA;

	case AVR_DOOR_CTRL_RECV_PAYLOAD:
		if (uart->recv_msg_pos < sizeof(msg->payload))
			msg->payload[uart->recv_msg_pos] = byte;
		uart->recv_msg_pos++;

		if (uart->recv_msg_pos >= msg->length) {
			uart->recv_state = AVR_DOOR_CTRL_RECV_CRC;
			uart->recv_msg_pos = 0;
		}
		return -ENODATA;

	case AVR_DOOR_CTRL_RECV_CRC:
		uart->recv_crc |= ((unsigned)byte) << (8 * uart->recv_msg_pos);
		uart->recv_msg_pos++;

		if (uart->recv_msg_pos < sizeof(uart->recv_crc))
			return -ENODATA;

		uart->recv_state = AVR_DOOR_CTRL_SYNC;
		if (uart->recv_crc != msg_compute_crc(msg)) {
			ULOG_WARN("Received message with a bad CRC: %x != %x!\n", uart->recv_crc, msg_compute_crc(msg));
			return -EBADMSG;
		}
		/* Got a message */
		return 1;

	default: /* Shouldn't happen */
		uart->recv_state = AVR_DOOR_CTRL_SYNC;
		return -ENODATA;
	}
}

static int uart_ctrl_transport_recv(struct avr_door_ctrl_transport *tr,
				    struct avr_door_ctrl_msg *msg)
{
	struct avr_door_ctrl_uart_transport *uart = container_of(
		tr, struct avr_door_ctrl_uart_transport, transport);
	int err = 0;

	/* Refill the buffer */
	if (uart->recv_buffer_pos == uart->recv_buffer_len) {
		do {
			err = read(tr->fd, uart->recv_buffer,
				   sizeof(uart->recv_buffer));
			/* Handle interrupted syscall */
			if (err < 0 && errno != EINTR)
				return -errno;
		} while (err < 0);

		uart->recv_buffer_len = err;
		uart->recv_buffer_pos = 0;
	}

	/* Parse the received data left in the buffer */
	while (uart->recv_buffer_pos < uart->recv_buffer_len) {
		err = uart_ctrl_transport_recv_byte(
			uart, uart->recv_buffer[uart->recv_buffer_pos++], msg);
		/* If we got a full message or an error return it */
		if (err != -ENODATA)
			return err;
	}

	return err;
}

static int uart_ctrl_transport_send(struct avr_door_ctrl_transport *tr,
				    const struct avr_door_ctrl_msg *msg)
{
	struct avr_door_ctrl_uart_transport *uart = container_of(
		tr, struct avr_door_ctrl_uart_transport, transport);
	int err = -ENODATA;

	/* Encode the new message */
	if (uart->send_buffer_pos == uart->send_buffer_len) {
		uart->send_buffer_len = msg_encode(msg, uart->send_buffer);
		uart->send_buffer_pos = 0;
	}

	while (uart->send_buffer_pos < uart->send_buffer_len) {
		err = write(tr->fd, uart->send_buffer + uart->send_buffer_pos,
		   uart->send_buffer_len - uart->send_buffer_pos);
		if (err > 0)
			uart->send_buffer_pos += err;
		else if (err == 0)
			return 0;
		else if (errno != EINTR)
			return -errno;
	}

	return 1;
}

static void uart_ctrl_transport_close(struct avr_door_ctrl_transport *tr)
{
	struct avr_door_ctrl_uart_transport *uart = container_of(
		tr, struct avr_door_ctrl_uart_transport, transport);

	close(tr->fd);
	free(uart);
}

int avr_door_ctrl_uart_transport_open(const char *dev, struct avr_door_ctrl_transport **tr)
{
	struct avr_door_ctrl_uart_transport *uart;
	struct termios attr;
	int fd, err;

	fd = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
	if (fd < 0)
		return -errno;

	/* Configure the TTY */
	err = tcgetattr(fd, &attr);
	if (err) {
		err = -errno;
		goto close_fd;
	}

	cfmakeraw(&attr);
	cfsetospeed(&attr, B38400);
	cfsetispeed(&attr, B38400);
	attr.c_cflag &= ~HUPCL;
	attr.c_cflag |= CREAD | CLOCAL;
	attr.c_iflag |= IGNBRK | IGNPAR;

	err = tcsetattr(fd, TCSANOW, &attr);
	if (err) {
		err = -errno;
		goto close_fd;
	}

	uart = calloc(1, sizeof(*uart));
	if (!uart) {
		err = -ENOMEM;
		goto close_fd;
	}

	uart->transport.fd = fd;
	uart->transport.recv = uart_ctrl_transport_recv;
	uart->transport.send = uart_ctrl_transport_send;
	uart->transport.close = uart_ctrl_transport_close;

	*tr = &uart->transport;

	return 0;

close_fd:
	close(fd);
	return err;

}
