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
struct avr_door_ctrl_request;

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

struct avr_door_ctrl_method_request {
	struct avr_door_ctrl_request req;

	const struct avr_door_ctrl_method *method;
	struct ubus_request_data uresp;
	struct blob_buf bbuf;
	void *query_ctx;
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

static void avr_door_ctrl_start_sending(struct avr_door_ctrl *ctrl)
{
	/* Add the fd to the writer list */
	uloop_fd_add(&ctrl->fd, ctrl->fd.flags | ULOOP_WRITE);
}

static void avr_door_ctrl_send_next_request(struct avr_door_ctrl *ctrl)
{
	/* Destroy the last request */
	if (ctrl->req) {
		if (ctrl->req->handlers->destroy)
			ctrl->req->handlers->destroy(ctrl->req);
		ctrl->req = NULL;
	}

	/* If the pending list is empty there is nothing to do */
	if (list_empty(&ctrl->pending_reqs))
		return;

	/* Get the next request out of the pending list */
	ctrl->req = list_first_entry(&ctrl->pending_reqs,
			       struct avr_door_ctrl_request, list);
	list_del_init(&ctrl->req->list);

	avr_door_ctrl_start_sending(ctrl);
}

static void avr_door_ctrl_complete_request(
	struct avr_door_ctrl_request *req, int status)
{
	struct avr_door_ctrl *ctrl = req->ctrl;

	uloop_timeout_cancel(&req->timeout);
	if (req->handlers->complete)
		req->handlers->complete(req, status);

	avr_door_ctrl_send_next_request(ctrl);
}

static void avr_door_ctrl_on_request_timeout(struct uloop_timeout *timeout)
{
	struct avr_door_ctrl_request *req = container_of(
		timeout, struct avr_door_ctrl_request, timeout);

	avr_door_ctrl_complete_request(req, -ETIMEDOUT);
}

static void avr_door_ctrl_request_init(
	struct avr_door_ctrl_request *req, struct avr_door_ctrl *ctrl,
	const struct avr_door_ctrl_request_handlers *handlers,
	unsigned msg_type, unsigned msg_length)
{
	req->ctrl = ctrl;
	req->handlers = handlers;
	req->timeout.cb = avr_door_ctrl_on_request_timeout;
	req->msg.type = msg_type;
	req->msg.length = msg_length;
}

static void avr_door_ctrl_request_send(struct avr_door_ctrl_request *req)
{
	struct avr_door_ctrl *ctrl = req->ctrl;

	/* Add the request to the pending list */
	list_add_tail(&req->list, &ctrl->pending_reqs);

	/* Send it out if no request is beeing sent */
	if (!ctrl->req)
		avr_door_ctrl_send_next_request(ctrl);
}

static int avr_door_ctrl_method_continue(
	struct avr_door_ctrl_method_request *req,
	const struct avr_door_ctrl_msg *resp) {
	int err;

	/* Check that the method do support continuing the request */
	if (!req->method->write_continue_query)
		return -EBADE;

	/* Update the request according to the response */
	err = req->method->write_continue_query(
		resp->payload, req->req.msg.payload, req->query_ctx);
	if (err)
		return err;

	/* Send the updated request */
	avr_door_ctrl_start_sending(req->req.ctrl);

	/* Indicate that the request is still in progress */
	return -EINPROGRESS;
}

static int avr_door_ctrl_method_on_response(
	struct avr_door_ctrl_request *request,
	const struct avr_door_ctrl_msg *resp) {
	struct avr_door_ctrl_method_request *req =
		container_of(request,
			     struct avr_door_ctrl_method_request, req);
	int err = 0;

	if (resp->length < req->method->response_size) {
		fprintf(stderr, "Received too short response\n");
		return -EINVAL;
	}

	if (req->method->read_response) {
		err = req->method->read_response(
			resp->payload, &req->bbuf, req->query_ctx);
		if (err == -EAGAIN)
			return avr_door_ctrl_method_continue(req, resp);
	}

	if (!err)
		err = ubus_send_reply(request->ctrl->daemon->uctx,
				      &req->uresp, req->bbuf.head);

	return err;
}

static void avr_door_ctrl_method_complete(
	struct avr_door_ctrl_request *request, int err) {
	struct avr_door_ctrl_method_request *req =
		container_of(request,
			     struct avr_door_ctrl_method_request, req);
	int status

	if (err == 0)
		status = UBUS_STATUS_OK;
	else if (err == -ETIMEDOUT)
		status = UBUS_STATUS_TIMEOUT;
	else
		status = UBUS_STATUS_UNKNOWN_ERROR;

	ubus_complete_deferred_request(request->ctrl->daemon->uctx,
				       &req->uresp, status);
}

static void avr_door_ctrl_method_destroy(struct avr_door_ctrl_request *request)
{
	struct avr_door_ctrl_method_request *req =
		container_of(request,
			     struct avr_door_ctrl_method_request, req);

	blob_buf_free(&req->bbuf);
	free(request);
}

static const
struct avr_door_ctrl_request_handlers avr_door_ctrl_method_handlers = {
	.on_response = avr_door_ctrl_method_on_response,
	.complete = avr_door_ctrl_method_complete,
	.destroy = avr_door_ctrl_method_destroy,
};

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
	struct avr_door_ctrl_method_request *req;
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

	avr_door_ctrl_request_init(
		&req->req, ctrl, &avr_door_ctrl_method_handlers,
		method->cmd, method->query_size);

	/* Setup the request */
	req->method = method;
	blob_buf_init(&req->bbuf, 0);

	if (method->write_query) {
		err = method->write_query(args, req->req.msg.payload,
					  &req->bbuf, &req->query_ctx);
		if (err) {
			free(req);
			return err;
		}
	}

	ubus_defer_request(ctrl->daemon->uctx, ureq, &req->uresp);

	avr_door_ctrl_request_send(&req->req);

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

	switch (msg->type) {
	case CTRL_CMD_OK:
		if (req->handlers->on_response) {
			err = req->handlers->on_response(req, msg);
			if (err == -EINPROGRESS)
				return;
		}
		break;
	case CTRL_CMD_ERROR:
		if (msg->length == 1) {
			err = (int8_t)msg->payload[0];
		} else {
			// Malformed response
			err = -EINVAL;
		}
		break;
	default:
		fprintf(stderr, "Invalid response\n");
		err = -EINVAL;
		break;
	}

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
					ctrl->req, -EINVAL);
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
		else if (err == 0) /* Handle EOF as an error */
			err = -ENOLINK;

		/* Disable the write events unless we must wait */
		if (err != -EAGAIN && err != -EWOULDBLOCK) {
			uloop_fd_add(&ctrl->fd, ctrl->fd.flags & ~ULOOP_WRITE);
			// TODO: log error
			if (err < 0)
				avr_door_ctrl_complete_request(ctrl->req, err);
		}
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
