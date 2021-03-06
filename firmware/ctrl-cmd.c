#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <avr/pgmspace.h>
#include "uart.h"
#include "uart-ctrl-transport.h"
#include "ctrl-cmd.h"
#include "eeprom.h"
#include "event-queue.h"
#include "utils.h"

struct ctrl_cmd_desc {
	uint8_t type;
	uint8_t length;
	int8_t (*handler)(struct ctrl_transport *ctrl, const void *payload);
};

static int8_t ctrl_cmd_get_device_descriptor(
	struct ctrl_transport *ctrl, const void *payload)
{
	struct device_descriptor desc = {
		.major_version = 0,
		.minor_version = 2,
		.num_doors = NUM_DOORS,
		.num_access_records = NUM_ACCESS_RECORDS,
		.free_access_records = eeprom_get_free_access_record_count(),
	};

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK,
				    &desc, sizeof(desc));
}

static int8_t ctrl_cmd_ping(
	struct ctrl_transport *ctrl, const void *payload)
{
	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, NULL, 0);
}

static int8_t ctrl_cmd_get_door_config(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct ctrl_cmd_get_door_config *get = payload;
	struct door_config cfg;
	int8_t err;

	err = eeprom_get_door_config(get->index, &cfg);
	if (err)
		return err;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK,
				    &cfg, sizeof(cfg));
}

static int8_t ctrl_cmd_set_door_config(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct ctrl_cmd_set_door_config *set = payload;
	int8_t err;

	err = eeprom_set_door_config(set->index, &set->config);
	if (err)
		return err;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, NULL, 0);
}

static int8_t ctrl_cmd_get_access_record(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct ctrl_cmd_get_access_record *get = payload;
	struct access_record record;
	int8_t err;

	err = eeprom_get_access_record(get->index, &record);
	if (err)
		return err;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK,
				    &record, sizeof(record));
}

static int8_t ctrl_cmd_set_access_record(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct ctrl_cmd_set_access_record *set = payload;
	int8_t err;

	err = eeprom_set_access_record(set->index, &set->record);
	if (err)
		return err;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, NULL, 0);
}

static int8_t ctrl_cmd_set_access(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct access_record *record = payload;
	int8_t err;

	err = eeprom_set_access(record->type, record->key, record->doors);
	if (err)
		return err;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, NULL, 0);
}

static int8_t ctrl_cmd_get_access(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct access_record *record = payload;
	uint8_t doors;
	int8_t err;

	err = eeprom_get_access(record->type, record->key, &doors);
	if (err)
		doors = 0;

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, &doors, sizeof(doors));
}

static int8_t ctrl_cmd_remove_all_access(
	struct ctrl_transport *ctrl, const void *payload)
{
	eeprom_remove_all_access();

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, NULL, 0);
}

static int8_t ctrl_cmd_get_used_access(
	struct ctrl_transport *ctrl, const void *payload)
{
	const struct ctrl_cmd_get_used_access *get = payload;
	struct ctrl_cmd_resp_used_access resp;
	int8_t err;
	uint16_t i;

	/* Go through all the records */
	for (i = get->start ; ; i++) {
		err = eeprom_get_access_record(i, &resp.record);
		/* Return an empty record when we reach the end */
		if (err == -ENOENT) {
			memset(&resp, 0, sizeof(resp));
			break;
		}
		if (err)
			return err;

		/* Ignore empty or unused records */
		if (resp.record.type == ACCESS_TYPE_NONE || !resp.record.used)
			continue;

		/* Clear the used flag */
		if (get->clear) {
			resp.record.used = 0;
			err = eeprom_set_access_record(i, &resp.record);
			if (err)
				return err;
		}

		resp.index = i;
		break;
	}

	return ctrl_transport_reply(ctrl, CTRL_CMD_OK, &resp, sizeof(resp));
}

static const struct ctrl_cmd_desc ctrl_cmd_desc[] PROGMEM = {
	{
		.type    = CTRL_CMD_GET_DEVICE_DESCRIPTOR,
		.length  = 0,
		.handler = ctrl_cmd_get_device_descriptor,
	},
	{
		.type    = CTRL_CMD_PING,
		.length  = 0,
		.handler = ctrl_cmd_ping,
	},
	{
		.type    = CTRL_CMD_GET_DOOR_CONFIG,
		.length  = sizeof(struct ctrl_cmd_get_door_config),
		.handler = ctrl_cmd_get_door_config,
	},
	{
		.type    = CTRL_CMD_SET_DOOR_CONFIG,
		.length  = sizeof(struct ctrl_cmd_set_door_config),
		.handler = ctrl_cmd_set_door_config,
	},
	{
		.type    = CTRL_CMD_GET_ACCESS_RECORD,
		.length  = sizeof(struct ctrl_cmd_get_access_record),
		.handler = ctrl_cmd_get_access_record,
	},
	{
		.type    = CTRL_CMD_SET_ACCESS_RECORD,
		.length  = sizeof(struct ctrl_cmd_set_access_record),
		.handler = ctrl_cmd_set_access_record,
	},
	{
		.type    = CTRL_CMD_SET_ACCESS,
		.length  = sizeof(struct access_record),
		.handler = ctrl_cmd_set_access,
	},
	{
		.type    = CTRL_CMD_GET_ACCESS,
		.length  = sizeof(struct access_record),
		.handler = ctrl_cmd_get_access,
	},
	{
		.type    = CTRL_CMD_REMOVE_ALL_ACCESS,
		.length  = 0,
		.handler = ctrl_cmd_remove_all_access,
	},
	{
		.type    = CTRL_CMD_GET_USED_ACCESS,
		.length  = sizeof(struct ctrl_cmd_get_used_access),
		.handler = ctrl_cmd_get_used_access,
	},
};

static void on_ctrl_transport_received_msg(
	struct ctrl_transport *ctrl, const struct ctrl_msg *msg)
{
	struct ctrl_cmd_desc desc;
	int8_t i, err;

	for (i = 0; i < ARRAY_SIZE(ctrl_cmd_desc); i++) {
		memcpy_P(&desc, &ctrl_cmd_desc[i], sizeof(desc));
		if (desc.type == msg->type)
			break;
	}

	if (i >= ARRAY_SIZE(ctrl_cmd_desc)) {
		err = -ENOENT;
		goto error;
	}

	if (msg->length != desc.length) {
		err = -EINVAL;
		goto error;
	}

	err = desc.handler(ctrl, msg->payload);
error:
	if (err)
		ctrl_transport_reply(ctrl, CTRL_CMD_ERROR,
				     &err, sizeof(err));
}

static void on_ctrl_transport_event(
	uint8_t event, union event_val val, void *context)
{
	struct ctrl_transport *ctrl = context;

	switch(event) {
	case CTRL_TRANSPORT_RECEIVED_MSG:
		on_ctrl_transport_received_msg(ctrl, val.data);
		break;
	}
}

static struct ctrl_transport ctrl_transport;
static struct event_handler ctrl_transport_handler = {
	.source = &ctrl_transport,
	.handler = on_ctrl_transport_event,
	.context = &ctrl_transport,
};

int8_t ctrl_cmd_init(void)
{
	int8_t err;

	err = ctrl_transport_init(&ctrl_transport);
	if (err)
		return err;

	return event_handler_add(&ctrl_transport_handler);
}

int8_t ctrl_send_event(uint8_t type, const void *payload, uint8_t length)
{
	return ctrl_transport_send_event(
		&ctrl_transport, type, payload, length);
}
