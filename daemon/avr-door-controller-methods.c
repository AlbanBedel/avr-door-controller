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

#include "avr-door-controller-daemon.h"
#include "../firmware/ctrl-cmd-types.h"
#include <endian.h>

static void pin_to_str(uint32_t key, char *pin)
{
	int i, j;

	for (i = 7, j = 0; i >= 0; i--) {
		uint8_t digit = (key >> (i * 4)) & 0xF;
		if (digit == 0xF)
			continue;
		pin[j++] = digit + '0';
	}
	pin[j] = 0;
}

static int pin_from_str(uint32_t *pin, const char *str)
{
	int i;

	*pin = 0xFFFFFFFF;

	for (i = 0; i < strlen(str); i++) {
		uint8_t digit = str[i] - '0';
		if (digit > 9)
			return -EINVAL;
		*pin = (*pin << 4) | digit;
	}

	return 0;
}

static const struct blobmsg_policy get_device_descriptor_args[] = {
};

static int read_get_device_descriptor_response(
	const void *response, struct blob_buf *bbuf)
{
	const struct device_descriptor *desc = response;

	blobmsg_add_u32(bbuf, "major_version", desc->major_version);
	blobmsg_add_u32(bbuf, "minor_version", desc->minor_version);
	blobmsg_add_u32(bbuf, "num_doors", desc->num_doors);
	blobmsg_add_u32(bbuf, "num_access_records",
			le16toh(desc->num_access_records));
	return 0;
}

static const struct blobmsg_policy get_door_config_args[] = {
	{
		.name = "index",
		.type = BLOBMSG_TYPE_INT32,
	},
};

static int write_get_door_config_query(
	struct blob_attr *const *const args,
	void *query, struct blob_buf *bbuf)
{
	struct ctrl_cmd_get_door_config *cmd = query;

	blobmsg_add_u32(bbuf, "index", blobmsg_get_u32(args[0]));
	cmd->index = blobmsg_get_u32(args[0]);
	return 0;
}

static int read_get_door_config_response(
	const void *response, struct blob_buf *bbuf)
{
	const struct door_config *cfg = (struct door_config *)response;

	blobmsg_add_u32(bbuf, "open_time", le16toh(cfg->open_time));
	return 0;
}

#define GET_ACCESS_RECORD_INDEX		0
#define GET_ACCESS_RECORD_PIN		1
#define GET_ACCESS_RECORD_CARD		2

static const struct blobmsg_policy get_access_record_args[] = {
	[GET_ACCESS_RECORD_INDEX] = {
		.name = "index",
		.type = BLOBMSG_TYPE_INT32,
	},
	[GET_ACCESS_RECORD_PIN] = {
		.name = "pin",
		.type = BLOBMSG_TYPE_STRING,
	},
	[GET_ACCESS_RECORD_CARD] = {
		.name = "card",
		.type = BLOBMSG_TYPE_INT32,
	},
};

static int write_get_access_record_query(
	struct blob_attr *const *const args,
	void *query, struct blob_buf *bbuf)
{
	struct ctrl_cmd_get_access_record *cmd = query;

	blobmsg_add_u32(bbuf, "index", blobmsg_get_u32(args[0]));

	if (args[GET_ACCESS_RECORD_PIN]) {
		char *pin = blobmsg_get_string(args[GET_ACCESS_RECORD_PIN]);

		if (pin)
			blobmsg_add_string(bbuf, "pin", pin);
	} else if (args[GET_ACCESS_RECORD_CARD]) {
		uint32_t card = blobmsg_get_u32(args[GET_ACCESS_RECORD_CARD]);

		blobmsg_add_u32(bbuf, "card", card);
	}

	cmd->index = htole16(blobmsg_get_u32(args[GET_ACCESS_RECORD_INDEX]));

	return 0;
}

static int read_get_access_record_response(
	const void *response, struct blob_buf *bbuf)
{
	const struct access_record *rec = (struct access_record *)response;
	struct blob_attr *args[ARRAY_SIZE(get_access_record_args)] = {};
	uint8_t perms, type, doors;
	char *str_pin;
	uint32_t key;
	char pin[9];

	/* The bit fields are broken with Chaos Calmer MIPS compiler */
	perms = ((uint8_t*)rec)[4];
	key = le32toh(rec->key);

	/* If the record is invalid ignore it */
	if (perms & BIT(2))
		perms = 0;

	type = perms & 0x3;
	doors = perms >> 4;

	if (type != ACCESS_TYPE_NONE)
		blobmsg_add_u32(bbuf, "doors", doors);

	switch (type) {
	case ACCESS_TYPE_PIN:
		pin_to_str(key, pin);
		blobmsg_add_string(bbuf, "pin", pin);
		break;
	case ACCESS_TYPE_CARD:
		blobmsg_add_u32(bbuf, "card", key);
		break;
	case ACCESS_TYPE_CARD_AND_PIN:
		blobmsg_parse(get_access_record_args,
			      ARRAY_SIZE(get_access_record_args), args,
			      blob_data(bbuf->head), blob_len(bbuf->head));
		str_pin = blobmsg_get_string(args[GET_ACCESS_RECORD_PIN]);
		if (str_pin) {
			uint32_t p;

			if (pin_from_str(&p, str_pin))
				return UBUS_STATUS_INVALID_ARGUMENT;
			blobmsg_add_u32(bbuf, "card", key ^ p);
		} else if (args[GET_ACCESS_RECORD_CARD]) {
			uint32_t card = blobmsg_get_u32(args[GET_ACCESS_RECORD_CARD]);

			pin_to_str(key ^ card, pin);
			blobmsg_add_string(bbuf, "pin", pin);
		} else {
			blobmsg_add_u32(bbuf, "card+pin", key);
		}
		break;
	}

	return 0;
}

#define SET_ACCESS_RECORD_INDEX		0
#define SET_ACCESS_RECORD_PIN		1
#define SET_ACCESS_RECORD_CARD		2
#define SET_ACCESS_RECORD_CARD_N_PIN	3
#define SET_ACCESS_RECORD_DOORS		4
#define SET_ACCESS_RECORD_ARGS_COUNT	ARRAY_SIZE(set_access_record_args)

static const struct blobmsg_policy set_access_record_args[] = {
	[SET_ACCESS_RECORD_INDEX] = {
		.name = "index",
		.type = BLOBMSG_TYPE_INT32,
	},
	[SET_ACCESS_RECORD_PIN] = {
		.name = "pin",
		.type = BLOBMSG_TYPE_STRING,
	},
	[SET_ACCESS_RECORD_CARD] = {
		.name = "card",
		.type = BLOBMSG_TYPE_INT32,
	},
	[SET_ACCESS_RECORD_CARD_N_PIN] = {
		.name = "card+pin",
		.type = BLOBMSG_TYPE_INT32,
	},
	[SET_ACCESS_RECORD_DOORS] = {
		.name = "doors",
		.type = BLOBMSG_TYPE_INT32,
	},
};

static int write_set_access_record_query(
	struct blob_attr *const *const args,
	void *query, struct blob_buf *bbuf)
{
	struct ctrl_cmd_set_access_record *cmd = query;
	uint32_t card = 0, pin = 0;
	uint8_t doors = 0;
	char *str_pin;
	uint8_t type;

	cmd->index = htole16(blobmsg_get_u32(args[SET_ACCESS_RECORD_INDEX]));

	str_pin = blobmsg_get_string(args[SET_ACCESS_RECORD_PIN]);
	if (args[SET_ACCESS_RECORD_CARD])
		card = blobmsg_get_u32(args[SET_ACCESS_RECORD_CARD]);
	if (args[SET_ACCESS_RECORD_CARD_N_PIN])
		card = blobmsg_get_u32(args[SET_ACCESS_RECORD_CARD_N_PIN]);
	if (args[SET_ACCESS_RECORD_DOORS])
		doors = blobmsg_get_u32(args[SET_ACCESS_RECORD_DOORS]) & 0xF;

	if (args[SET_ACCESS_RECORD_CARD_N_PIN] ||
	    (args[SET_ACCESS_RECORD_CARD] && str_pin))
		type = ACCESS_TYPE_CARD_AND_PIN;
	else if (args[SET_ACCESS_RECORD_CARD])
		type = ACCESS_TYPE_CARD;
	else if (str_pin)
		type = ACCESS_TYPE_PIN;
	else
		type = ACCESS_TYPE_NONE;

	switch (type) {
	case ACCESS_TYPE_NONE:
	case ACCESS_TYPE_CARD:
		break;

	case ACCESS_TYPE_CARD_AND_PIN:
		if (args[SET_ACCESS_RECORD_CARD_N_PIN])
			break;
	case ACCESS_TYPE_PIN:
		if (pin_from_str(&pin, str_pin))
			return UBUS_STATUS_INVALID_ARGUMENT;
		break;

	default:
		return UBUS_STATUS_INVALID_ARGUMENT;
	}

	cmd->record.key = htole32(card ^ pin);
	((uint8_t*)&cmd->record)[4] = (doors << 4) | type;

	return 0;
}

#define SET_ACCESS_PIN		0
#define SET_ACCESS_CARD		1
#define SET_ACCESS_DOORS	2
#define SET_ACCESS_ARGS_COUNT	ARRAY_SIZE(set_access_args)

static const struct blobmsg_policy set_access_args[] = {
	[SET_ACCESS_PIN] = {
		.name = "pin",
		.type = BLOBMSG_TYPE_STRING,
	},
	[SET_ACCESS_CARD] = {
		.name = "card",
		.type = BLOBMSG_TYPE_INT32,
	},
	[SET_ACCESS_DOORS] = {
		.name = "doors",
		.type = BLOBMSG_TYPE_INT32,
	},
};

static int write_set_access_query(
	struct blob_attr *const *const args,
	void *query, struct blob_buf *bbuf)
{
	struct access_record *rec = query;
	uint32_t card = 0, pin = 0;
	uint8_t doors = 0;
	char *str_pin;
	uint8_t type;

	str_pin = blobmsg_get_string(args[SET_ACCESS_PIN]);
	if (args[SET_ACCESS_CARD])
		card = blobmsg_get_u32(args[SET_ACCESS_CARD]);
	if (args[SET_ACCESS_DOORS])
		doors = blobmsg_get_u32(args[SET_ACCESS_DOORS]) & 0xF;

	if (args[SET_ACCESS_CARD] && str_pin)
		type = ACCESS_TYPE_CARD_AND_PIN;
	else if (args[SET_ACCESS_CARD])
		type = ACCESS_TYPE_CARD;
	else if (str_pin)
		type = ACCESS_TYPE_PIN;
	else
		return UBUS_STATUS_INVALID_ARGUMENT;

	switch (type) {
	case ACCESS_TYPE_CARD_AND_PIN:
	case ACCESS_TYPE_PIN:
		if (pin_from_str(&pin, str_pin))
			return UBUS_STATUS_INVALID_ARGUMENT;
		break;
	}

	rec->key = htole32(card ^ pin);
	((uint8_t*)rec)[4] = (doors << 4) | type;

	return 0;
}

static const struct blobmsg_policy remove_all_access_args[] = {
};

#define AVR_DOOR_CTRL_METHOD(method, opt_args, cmd_id,			\
			     wr_query, qr_size, rd_resp, resp_size)	\
	{								\
		.name = #method,					\
		.args = method ## _args,				\
		.num_args = ARRAY_SIZE(method ## _args),		\
		.optional_args = opt_args,				\
		.cmd = cmd_id,				       		\
		.write_query = wr_query,				\
		.query_size = qr_size,					\
		.read_response = rd_resp,				\
		.response_size = resp_size,				\
	}

const struct avr_door_ctrl_method avr_door_ctrl_methods[] = {
	AVR_DOOR_CTRL_METHOD(
		get_device_descriptor, 0,
		CTRL_CMD_GET_DEVICE_DESCRIPTOR,
		NULL, 0,
		read_get_device_descriptor_response,
		sizeof(struct device_descriptor)),

	AVR_DOOR_CTRL_METHOD(
		get_door_config, 0,
		CTRL_CMD_GET_DOOR_CONFIG,
		write_get_door_config_query,
		sizeof(struct ctrl_cmd_get_door_config),
		read_get_door_config_response,
		sizeof(struct door_config)),

	AVR_DOOR_CTRL_METHOD(
		get_access_record,
		BIT(GET_ACCESS_RECORD_PIN) |
		BIT(GET_ACCESS_RECORD_CARD),
		CTRL_CMD_GET_ACCESS_RECORD,
		write_get_access_record_query,
		sizeof(struct ctrl_cmd_get_access_record),
		read_get_access_record_response,
		sizeof(struct access_record)),

	AVR_DOOR_CTRL_METHOD(
		set_access_record,
		BIT(SET_ACCESS_RECORD_PIN) |
		BIT(SET_ACCESS_RECORD_CARD) |
		BIT(SET_ACCESS_RECORD_CARD_N_PIN) |
		BIT(SET_ACCESS_RECORD_DOORS),
		CTRL_CMD_SET_ACCESS_RECORD,
		write_set_access_record_query,
		sizeof(struct ctrl_cmd_set_access_record),
		NULL, 0),

	AVR_DOOR_CTRL_METHOD(
		set_access,
		BIT(SET_ACCESS_PIN) |
		BIT(SET_ACCESS_CARD) |
		BIT(SET_ACCESS_DOORS),
		CTRL_CMD_SET_ACCESS,
		write_set_access_query,
		sizeof(struct access_record),
		NULL, 0),

	AVR_DOOR_CTRL_METHOD(
		remove_all_access, 0,
		CTRL_CMD_REMOVE_ALL_ACCESS,
		NULL, 0, NULL, 0),
};

const struct avr_door_ctrl_method *avr_door_ctrl_get_method(const char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(avr_door_ctrl_methods); i++)
		if (!strcmp(avr_door_ctrl_methods[i].name, name))
			return &avr_door_ctrl_methods[i];

	return NULL;
}

static struct ubus_method
avr_door_ctrl_umethods[ARRAY_SIZE(avr_door_ctrl_methods)] = {};

static struct ubus_object_type avr_door_ctrl_utype =
	UBUS_OBJECT_TYPE("door_ctrl", avr_door_ctrl_umethods);


void avr_door_ctrld_init_door_uobject(
	const char *name, struct ubus_object *uobj)
{
	static bool umethods_inited = false;
	int i;

	if (!umethods_inited) {
		for (i = 0; i < ARRAY_SIZE(avr_door_ctrl_methods); i++) {
			const struct avr_door_ctrl_method *m =
				&avr_door_ctrl_methods[i];
			struct ubus_method *u =
				&avr_door_ctrl_umethods[i];
			u->name = m->name;
			u->handler = avr_door_ctrl_method_handler;
			u->policy = m->args;
			u->n_policy = m->num_args;
		}
		umethods_inited = true;
	}

	uobj->name = name;
	uobj->type = &avr_door_ctrl_utype;
	uobj->methods = avr_door_ctrl_umethods;
	uobj->n_methods = ARRAY_SIZE(avr_door_ctrl_umethods);
}
