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
#ifndef AVR_DOOR_CONTROLLER_DAEMON_H
#define AVR_DOOR_CONTROLLER_DAEMON_H

#include <stdint.h>
#include <libubus.h>

#define AVR_DOOR_CTRL_MSG_MAX_PAYLOAD_SIZE	16
#define AVR_DOOR_CTRL_METHOD_MAX_ARGS		8

struct avr_door_ctrld;

struct avr_door_ctrl_msg {
	uint8_t type;
	uint8_t length;
	uint8_t payload[AVR_DOOR_CTRL_MSG_MAX_PAYLOAD_SIZE];
};

struct avr_door_ctrl_method {
	/* Ubus side */
	const char *name;
	const struct blobmsg_policy *args;
	unsigned int num_args;
	/* Bitmask of the optional arguments */
	unsigned int optional_args;

	/* Controller side */
	unsigned int cmd;

	/* Convert a ubus query to controller command */
	int (*write_query)(struct blob_attr *const *const args,
			   void *query, struct blob_buf *bbuf);
	unsigned int query_size;

	/* Convert a controller response to a ubus one */
	int (*read_response)(const void *response, struct blob_buf *bbuf);
	unsigned int response_size;
};

int avr_door_ctrl_method_handler(
	struct ubus_context *uctx, struct ubus_object *uobj,
	struct ubus_request_data *ureq, const char *method_name,
	struct blob_attr *msg);

const struct avr_door_ctrl_method *avr_door_ctrl_get_method(const char *name);
void avr_door_ctrld_init_door_uobject(
	const char *name, struct ubus_object *uobj);

struct avr_door_ctrl_transport {
	int fd;

	int (*send)(struct avr_door_ctrl_transport *tr,
		    const struct avr_door_ctrl_msg *msg);
	int (*recv)(struct avr_door_ctrl_transport *tr,
		    struct avr_door_ctrl_msg *msg);
	void (*close)(struct avr_door_ctrl_transport *tr);
};

int avr_door_ctrl_uart_transport_open(
	const char *dev, struct avr_door_ctrl_transport **tr);

#endif /* AVR_DOOR_CONTROLLER_DAEMON_H */
