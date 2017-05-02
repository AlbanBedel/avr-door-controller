#ifndef UART_CTRL_TRANSPORT_H
#define UART_CTRL_TRANSPORT_H

#include "ctrl-cmd-types.h"
#include "ctrl-transport.h"

/* The UART transport encode the messages as follow:
 *
 * The messages are prefixed a start byte (0x7E).
 * If 0x7E or 0x7D appear in the message they are escaped with 0x7D followed
 * by the original byte xor'ed with 0x20.
 * The message is followed by an xmodem CRC in little endian format.
 */

#define UART_CTRL_TRANSPORT_START		0x7E
#define UART_CTRL_TRANSPORT_ESC			0x7D
#define UART_CTRL_TRANSPORT_UNESCAPE(x)		((x) ^ 0x20)
#define UART_CTRL_TRANSPORT_ESCAPE(x)		UART_CTRL_TRANSPORT_UNESCAPE(x)

struct ctrl_transport {
	uint8_t state  : 7;
	uint8_t escape : 1;
	uint8_t pos;
	uint16_t computed_crc;
	uint16_t msg_crc;
	union {
		struct ctrl_msg msg;
		uint8_t outbuf[1 + sizeof(struct ctrl_msg) * 2];
	};
};

#endif /* UART_CTRL_TRANSPORT_H */
