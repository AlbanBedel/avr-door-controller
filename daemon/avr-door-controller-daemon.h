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
struct avr_door_ctrl_request;
struct avr_door_ctrl;

struct avr_door_ctrl_msg {
	uint8_t type;
	uint8_t length;
	uint8_t payload[AVR_DOOR_CTRL_MSG_MAX_PAYLOAD_SIZE];
};

struct avr_door_ctrl_request_handlers {
	int (*on_response)(struct avr_door_ctrl_request *req,
			   const struct avr_door_ctrl_msg *msg);
	void (*complete)(struct avr_door_ctrl_request *req, int err);
	void (*destroy)(struct avr_door_ctrl_request *req);
};

struct avr_door_ctrl_request {
	struct list_head list;
	struct avr_door_ctrl *ctrl;
	struct avr_door_ctrl_msg msg;
	struct uloop_timeout timeout;
	const struct avr_door_ctrl_request_handlers *handlers;
};

void avr_door_ctrl_request_init(
	struct avr_door_ctrl_request *req, struct avr_door_ctrl *ctrl,
	const struct avr_door_ctrl_request_handlers *handlers,
	unsigned msg_type, unsigned msg_length);

void avr_door_ctrl_request_send(struct avr_door_ctrl_request *req);

struct avr_door_ctrl {
	/* Name of this controller object */
	char name[64];
	/* List of avr_door_ctrl */
	struct list_head list;
	/* Daemon object for this door ctrl */
	struct avr_door_ctrld *daemon;
	/* Transport for this controller */
	struct avr_door_ctrl_transport *transport;

	/* uloop fd for this controller */
	struct uloop_fd fd;
	/* uobject representing this controller */
	struct ubus_object uobject;

	/* Incoming message */
	struct avr_door_ctrl_msg msg;
	unsigned int msg_pos;

	/* List of pending requests */
	struct list_head pending_reqs;
	/* Request currently processed */
	struct avr_door_ctrl_request *req;

	/* Timeout to trigger sending ping commands */
	struct uloop_timeout ping_timeout;
};

void avr_door_ctrl_start_sending(struct avr_door_ctrl *ctrl);

void avr_door_ctrld_init_door_uobject(
	const char *name, struct ubus_object *uobj);

struct avr_door_ctrl_transport {
	int fd;

	int (*send)(struct avr_door_ctrl_transport *tr,
		    const struct avr_door_ctrl_msg *msg);
	int (*recv)(struct avr_door_ctrl_transport *tr,
		    struct avr_door_ctrl_msg *msg);
	int (*reset)(struct avr_door_ctrl_transport *tr);
	void (*close)(struct avr_door_ctrl_transport *tr);
};

int avr_door_ctrl_uart_transport_open(
	const char *dev, struct avr_door_ctrl_transport **tr);

#endif /* AVR_DOOR_CONTROLLER_DAEMON_H */
