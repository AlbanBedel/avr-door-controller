#include <errno.h>
#include <string.h>
#include <util/crc16.h>

#include "uart.h"
#include "uart-ctrl-transport.h"
#include "ctrl-cmd.h"
#include "event-queue.h"

#define UART_CTRL_TRANSPORT_SYNC		0
#define UART_CTRL_TRANSPORT_RECV_TYPE		1
#define UART_CTRL_TRANSPORT_RECV_LENGTH		2
#define UART_CTRL_TRANSPORT_RECV_PAYLOAD	3
#define UART_CTRL_TRANSPORT_RECV_CRC		4
#define UART_CTRL_TRANSPORT_WAIT_FOR_REPLY	5
#define UART_CTRL_TRANSPORT_SEND_REPLY		6

#define UART_CTRL_TRANSPORT_CRC_INIT		0
#define uart_ctrl_transport_crc(c, b)		_crc_xmodem_update(c, b)

static void uart_ctrl_transport_on_recv(uint8_t byte, void *context)
{
	struct ctrl_transport *ctrl = context;
	struct ctrl_msg *msg = &ctrl->msg;
	int8_t err;

	/* Ignore any incomming data while responding */
	if (ctrl->state >= UART_CTRL_TRANSPORT_WAIT_FOR_REPLY)
		return;

	/* Handle a start byte */
	if (byte == UART_CTRL_TRANSPORT_START) {
		ctrl->state = UART_CTRL_TRANSPORT_RECV_TYPE;
		ctrl->pos = 0;
		ctrl->escape = 0;
		ctrl->computed_crc = UART_CTRL_TRANSPORT_CRC_INIT;
		return;
	}

	/* While not in sync ignore everything */
	if (ctrl->state == UART_CTRL_TRANSPORT_SYNC)
		return;

	/* Unescape */
	if (ctrl->escape) {
		byte = UART_CTRL_TRANSPORT_UNESCAPE(byte);
		ctrl->escape = 0;
	} else if (byte == UART_CTRL_TRANSPORT_ESC) {
		ctrl->escape = 1;
		return;
	}

	/* Compute the CRC of the incoming data */
	if (ctrl->state < UART_CTRL_TRANSPORT_RECV_CRC)
		ctrl->computed_crc = uart_ctrl_transport_crc(
			ctrl->computed_crc, byte);

	/* Process the incoming message */
	switch (ctrl->state) {
	case UART_CTRL_TRANSPORT_RECV_TYPE:
		msg->type = byte;
		ctrl->state = UART_CTRL_TRANSPORT_RECV_LENGTH;
		return;

	case UART_CTRL_TRANSPORT_RECV_LENGTH:
		msg->length = byte;
		if (msg->length > 0)
			ctrl->state = UART_CTRL_TRANSPORT_RECV_PAYLOAD;
		else
			ctrl->state = UART_CTRL_TRANSPORT_RECV_CRC;
		ctrl->pos = 0;
		return;

	case UART_CTRL_TRANSPORT_RECV_PAYLOAD:
		if (ctrl->pos < sizeof(msg->payload))
			msg->payload[ctrl->pos++] = byte;
		if (ctrl->pos >= msg->length) {
			ctrl->state = UART_CTRL_TRANSPORT_RECV_CRC;
			ctrl->pos = 0;
		}
		return;

	case UART_CTRL_TRANSPORT_RECV_CRC:
		((uint8_t *)&ctrl->msg_crc)[ctrl->pos++] = byte;
		if (ctrl->pos < sizeof(ctrl->msg_crc))
			return;

		ctrl->state = UART_CTRL_TRANSPORT_WAIT_FOR_REPLY;

		if (msg->length > sizeof(msg->payload))
			err = -E2BIG;
		else if (ctrl->computed_crc != ctrl->msg_crc)
			err = -EINVAL;
		else
			err = 0;

		if (err)
			ctrl_transport_reply(ctrl, CTRL_CMD_ERROR,
					     &err, sizeof(err));
		else
			event_add(ctrl, CTRL_TRANSPORT_RECEIVED_MSG,
				  EVENT_PTR(msg));
		return;
	}
}

static void uart_ctrl_transport_on_reply_sent(void *context)
{
	struct ctrl_transport *ctrl = context;

	ctrl->state = UART_CTRL_TRANSPORT_SYNC;
}

static int8_t uart_ctrl_transport_write_outbuf(struct ctrl_transport *ctrl,
				     uint16_t *crc, uint8_t c)
{
	uint8_t esc = (c == UART_CTRL_TRANSPORT_START ||
		       c == UART_CTRL_TRANSPORT_ESC);

	if (ctrl->pos + esc >= sizeof(ctrl->outbuf))
		return -E2BIG;

	if (esc) {
		ctrl->outbuf[ctrl->pos++] = UART_CTRL_TRANSPORT_ESC;
		ctrl->outbuf[ctrl->pos++] = UART_CTRL_TRANSPORT_ESCAPE(c);
	} else {
		ctrl->outbuf[ctrl->pos++] = c;
	}

	if (crc)
		*crc = uart_ctrl_transport_crc(*crc, c);

	return 0;
}

int8_t ctrl_transport_reply(struct ctrl_transport *ctrl, uint8_t type,
			    const void *payload, uint8_t length)
{
	struct ctrl_msg *msg = &ctrl->msg;
	uint16_t crc = UART_CTRL_TRANSPORT_CRC_INIT;
	int8_t err;
	uint8_t i;

	if (ctrl->state < UART_CTRL_TRANSPORT_WAIT_FOR_REPLY)
		return -EINVAL;
	if (ctrl->state > UART_CTRL_TRANSPORT_WAIT_FOR_REPLY)
		return -EBUSY;
	if (length > sizeof(msg->payload))
		return -E2BIG;

	/* Initialize the state and write the start byte */
	ctrl->pos = 0;
	ctrl->outbuf[ctrl->pos++] = UART_CTRL_TRANSPORT_START;
	/* Write the packet header */
	uart_ctrl_transport_write_outbuf(ctrl, &crc, type);
	uart_ctrl_transport_write_outbuf(ctrl, &crc, length);
	/* Then the payload */
	for (i = 0; i < length; i++) {
		err = uart_ctrl_transport_write_outbuf(
			ctrl, &crc, ((uint8_t *)payload)[i]);
		if (err)
			return err;
	}
	/* Finally the CRC */
	for (i = 0; i < sizeof(crc); i++) {
		err = uart_ctrl_transport_write_outbuf(
			ctrl, NULL, ((uint8_t *)&crc)[i]);
		if (err)
			return err;
	}

	/* Then send the whole message */
	err = uart_send(ctrl->outbuf, ctrl->pos,
			uart_ctrl_transport_on_reply_sent, ctrl);
	if (err)
		ctrl->state = UART_CTRL_TRANSPORT_SYNC;
	else
		ctrl->state = UART_CTRL_TRANSPORT_SEND_REPLY;

	return err;
}

int8_t ctrl_transport_init(struct ctrl_transport *ctrl)
{
	int8_t err;

	memset(ctrl, 0, sizeof(*ctrl));

	err = uart_init(UART_DIRECTION_BOTH, 38400, 1, UART_PARITY_NONE);
	if (err)
		return err;

	return uart_set_recv_handler(uart_ctrl_transport_on_recv, ctrl);
}
