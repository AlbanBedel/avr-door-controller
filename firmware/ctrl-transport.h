#ifndef CTRL_TRANSPORT_H
#define CTRL_TRANSPORT_H

#include <stdint.h>

struct ctrl_transport;

/* Data is a pointer to a struct uart_ctrl_msg */
#define CTRL_TRANSPORT_RECEIVED_MSG		0

int8_t ctrl_transport_init(struct ctrl_transport *ctrl);

/* Send a reply to a command */
int8_t ctrl_transport_reply(struct ctrl_transport *ctrl, uint8_t type,
			    const void *payload, uint8_t length);

/* Send an event */
int8_t ctrl_transport_send_event(struct ctrl_transport *ctrl, uint8_t type,
				 const void *payload, uint8_t length);

#endif /* CTRL_TRANSPORT_H */
