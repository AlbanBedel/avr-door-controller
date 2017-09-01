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

#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>

#include <libubox/ulog.h>
#include <libubus.h>

#include "avr-door-controller-daemon.h"
#include "../firmware/ctrl-cmd-types.h"

#define AVR_DOOR_CTRL_REQUEST_TIMEOUT 500

struct avr_door_ctrld;

struct avr_door_ctrl_request {
	struct list_head list;
	struct avr_door_ctrl *ctrl;
	const struct avr_door_ctrl_method *method;

	struct avr_door_ctrl_msg msg;
	struct uloop_timeout timeout;
	struct ubus_request_data uresp;
	struct blob_buf bbuf;
};

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
};

struct avr_door_ctrld {
	struct ubus_context *uctx;
	struct list_head ctrls;
};

static void avr_door_ctrl_send_next_request(struct avr_door_ctrl *ctrl)
{
	/* Destroy the last request */
	if (ctrl->req) {
		blob_buf_free(&ctrl->req->bbuf);
		free(ctrl->req);
		ctrl->req = NULL;
	}

	/* If the pending list is empty there is nothing to do */
	if (list_empty(&ctrl->pending_reqs))
		return;

	/* Get the next request out of the pending list */
	ctrl->req = list_first_entry(&ctrl->pending_reqs,
			       struct avr_door_ctrl_request, list);
	list_del_init(&ctrl->req->list);

	/* Add the fd to the writer list */
	uloop_fd_add(&ctrl->fd, ctrl->fd.flags | ULOOP_WRITE);
}

static void avr_door_ctrl_complete_request(
	struct avr_door_ctrl_request *req, int status)
{
	uloop_timeout_cancel(&req->timeout);
	ubus_complete_deferred_request(
		req->ctrl->daemon->uctx, &req->uresp, status);
	avr_door_ctrl_send_next_request(req->ctrl);
}

static void avr_door_ctrl_on_request_timeout(struct uloop_timeout *timeout)
{
	struct avr_door_ctrl_request *req = container_of(
		timeout, struct avr_door_ctrl_request, timeout);

	avr_door_ctrl_complete_request(req, UBUS_STATUS_TIMEOUT);
}

int avr_door_ctrl_method_handler(
	struct ubus_context *uctx, struct ubus_object *uobj,
	struct ubus_request_data *ureq, const char *method_name,
	struct blob_attr *msg)
{
	struct blob_attr *args[AVR_DOOR_CTRL_METHOD_MAX_ARGS];
	const struct avr_door_ctrl_method *method =
		avr_door_ctrl_get_method(method_name);
	struct avr_door_ctrl *ctrl = container_of(
		uobj, struct avr_door_ctrl, uobject);
	struct avr_door_ctrl_request *req;
	int i, err;

	if (!method) {
		// LOG ERROR
		return UBUS_STATUS_UNKNOWN_ERROR;
	}

	/* Read the arguments */
	if (method->num_args > 0) {
		err = blobmsg_parse(method->args, method->num_args,
				    args, blob_data(msg), blob_len(msg));
		if (err) {
			// LOG ERROR
			return UBUS_STATUS_INVALID_ARGUMENT;
		}

		/* Check that all required arguments are there */
		for (i = 0; i < method->num_args; i++)
			if (!(method->optional_args & BIT(i)) && !args[i])
				return UBUS_STATUS_INVALID_ARGUMENT;
	}

	req = calloc(1, sizeof(*req));
	if (!req)
		return UBUS_STATUS_UNKNOWN_ERROR;

	/* Setup the request */
	req->ctrl = ctrl;
	req->method = method;
	req->timeout.cb = avr_door_ctrl_on_request_timeout;
	blob_buf_init(&req->bbuf, 0);

	/* Write the contorl request */
	req->msg.type = method->cmd;
	req->msg.length = method->query_size;

	if (method->write_query) {
		err = method->write_query(args, req->msg.payload, &req->bbuf);
		if (err) {
			free(req);
			return err;
		}
	}

	/* Add the request to pending list */
	ubus_defer_request(ctrl->daemon->uctx, ureq, &req->uresp);
	list_add_tail(&req->list, &ctrl->pending_reqs);

	/* Send it out if no request is beeing sent */
	if (!ctrl->req)
		avr_door_ctrl_send_next_request(ctrl);

	return 0;
}

static void avr_door_ctrl_recv_msg(
	struct avr_door_ctrl *ctrl, struct avr_door_ctrl_msg *msg)
{
	struct avr_door_ctrl_request *req = ctrl->req;
	int err = 0;

	if (!req) {
		fprintf(stderr, "Got message, but no request is pending\n");
		return;
	}

	uloop_timeout_cancel(&req->timeout);

	if (msg->type != CTRL_CMD_OK) {
		// LOG bad response size
		fprintf(stderr, "Received error %d\n",
			(int)(int8_t)msg->payload[0]);
		err = UBUS_STATUS_UNKNOWN_ERROR;
		goto complete_request;
	}

	if (msg->length < req->method->response_size) {
		fprintf(stderr, "Received too short response\n");
		err = UBUS_STATUS_UNKNOWN_ERROR;
		goto complete_request;
	}

	if (req->method->read_response)
		err = req->method->read_response(msg->payload, &req->bbuf);
	if (!err)
		err = ubus_send_reply(ctrl->daemon->uctx,
				      &req->uresp, req->bbuf.head);

complete_request:
	avr_door_ctrl_complete_request(ctrl->req, err);
}

static void avr_door_ctrl_on_transport_event(
	struct uloop_fd *fd, unsigned int events)
{
	struct avr_door_ctrl *ctrl =
		container_of(fd, struct avr_door_ctrl, fd);
	int err;

	if (events & ULOOP_READ) {
		err = ctrl->transport->recv(ctrl->transport, &ctrl->msg);
		if (err > 0) {
			avr_door_ctrl_recv_msg(ctrl, &ctrl->msg);
		} else if (err == 0) {
			// TODO: handle EOF
		} else if (err == -ENODATA) {
			/* No full message yet */
		} else if (err == -EAGAIN || err == -EWOULDBLOCK) {
			/* No data available anymore */
		} else if (err == -EBADMSG) {
			/* Terminate the request */
			if (ctrl->req)
				avr_door_ctrl_complete_request(
					ctrl->req, UBUS_STATUS_UNKNOWN_ERROR);
		} else {
			// TODO: log error
		}
	}

	if (events & ULOOP_WRITE) {
		if (!ctrl->req) {
			uloop_fd_add(&ctrl->fd, ctrl->fd.flags & ~ULOOP_WRITE);
			return;
		}

		err = ctrl->transport->send(ctrl->transport, &ctrl->req->msg);
		/* If we finished writing the message, wait for the anwser */
		if (err > 0)
			uloop_timeout_set(&ctrl->req->timeout,
					  AVR_DOOR_CTRL_REQUEST_TIMEOUT);

		/* Disable the write events unless we must wait */
		if (err != -EAGAIN && err != -EWOULDBLOCK)
			uloop_fd_add(&ctrl->fd, ctrl->fd.flags & ~ULOOP_WRITE);
		// TODO: log error
	}
}

int avr_door_ctrld_add_device(struct avr_door_ctrld *ctrld,
			     const char *name, const char *path)
{
	struct avr_door_ctrl *ctrl;
	int err;

	ctrl = calloc(1, sizeof(*ctrl));
	if (!ctrl)
		return -ENOMEM;

	snprintf(ctrl->name, sizeof(ctrl->name), "doors.%s", name);
	ctrl->daemon = ctrld;
	INIT_LIST_HEAD(&ctrl->list);
	INIT_LIST_HEAD(&ctrl->pending_reqs);

	err = avr_door_ctrl_uart_transport_open(path, &ctrl->transport);
	if (err) {
		ULOG_ERR("Failed to open UART transport %s: %s\n",
			 path, strerror(-err));
		return err;
	}

	ctrl->fd.fd = ctrl->transport->fd;
	ctrl->fd.cb = avr_door_ctrl_on_transport_event;

	/* Register the fd in uloop */
	err = uloop_fd_add(&ctrl->fd, ULOOP_READ);
	if (err) {
		ULOG_ERR("Failed to register transport FD of %s\n", name);
		goto close_transport;
	}

	/* Register the ubus object */
	avr_door_ctrld_init_door_uobject(ctrl->name, &ctrl->uobject);

	err = ubus_add_object(ctrld->uctx, &ctrl->uobject);
	if (err) {
		ULOG_ERR("Failed to add object %s\n", ctrl->uobject.name);
		goto uloop_delete;
	}

	list_add_tail(&ctrl->list, &ctrld->ctrls);

	return 0;

uloop_delete:
	uloop_fd_delete(&ctrl->fd);
close_transport:
	ctrl->transport->close(ctrl->transport);
	free(ctrl);
	return err;
}

int avr_door_ctrld_init(struct avr_door_ctrld *ctrld, const char *ubus_socket)
{
	INIT_LIST_HEAD(&ctrld->ctrls);

	ctrld->uctx = ubus_connect(ubus_socket);
	if (!ctrld->uctx) {
		fprintf(stderr, "Failed to connect to ubus\n");
		return -ENODEV;
	}

	ubus_add_uloop(ctrld->uctx);

	return 0;
}

void avr_door_ctrld_uninit(struct avr_door_ctrld *ctrld)
{
	// TODO: Remove all devices
	ubus_free(ctrld->uctx);
}

void usage(const char *progname, int ret)
{
	fprintf(stderr, "Usage: %s [-h | -s PATH] NAME PATH...\n", progname);
	exit(ret);
}

int main(int argc, char **argv)
{
	struct avr_door_ctrld ctrld = {};
	const char *ubus_socket = NULL;
	int i, opt, err = 0;

	while ((opt = getopt(argc, argv, "hs:")) != -1) {
		switch (opt) {
		case 's':
			ubus_socket = optarg;
			break;
		case 'h':
			usage(argv[0], 0);
			break;
		}
	}

	if ((argc - optind) % 2)
		usage(argv[0], 1);

	uloop_init();

	avr_door_ctrld_init(&ctrld, ubus_socket);
	for (i = optind; i < argc; i += 2) {
		err = avr_door_ctrld_add_device(&ctrld, argv[i], argv[i + 1]);
		if (err) {
			fprintf(stderr, "Failed to add device %s (%s): %s\n",
				argv[i], argv[i + 1], strerror(-err));
			break;
		} else {
			fprintf(stderr, "Added device %s (%s)\n",
				argv[i], argv[i + 1]);
		}
	}

	if (!err) {
		fprintf(stderr, "Starting uloop\n");
		uloop_run();
	}

	fprintf(stderr, "Exiting!\n");
	avr_door_ctrld_uninit(&ctrld);
	uloop_done();

	return 0;
}
