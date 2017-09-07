#ifndef DOOR_CONTROLLER_H
#define DOOR_CONTROLLER_H

#include <stdint.h>
#include "wiegand-reader.h"
#include "event-queue.h"
#include "trigger.h"
#include "timer.h"
#include "eeprom.h"

#define DOOR_CTRL_CARD		ACCESS_TYPE_CARD
#define DOOR_CTRL_PIN		ACCESS_TYPE_PIN
#define DOOR_CTRL_CARD_AND_PIN	ACCESS_TYPE_CARD_AND_PIN

typedef int8_t (*door_ctrl_check)(
	uint8_t door_id, uint8_t type, uint32_t key, void *context);

struct door_ctrl_config {
	uint8_t door_id;

	uint8_t d0_irq;
	uint8_t d1_irq;

	uint16_t open_time;

	uint8_t open_gpio;
	uint8_t led_gpio;
	uint8_t buzzer_gpio;


	door_ctrl_check check_key;
	void *check_context;
};

enum door_state {
	DOOR_CTRL_IDLE,
	DOOR_CTRL_READING_PIN,
	DOOR_CTRL_OPENING,
	DOOR_CTRL_REJECTED,
	DOOR_CTRL_TIMEOUT,
	DOOR_CTRL_ERROR,
};

#define DOOR_CTRL_EVENT_STATE_CHANGED		10
#define DOOR_CTRL_EVENT_BUZZER_FINISHED		11
#define DOOR_CTRL_EVENT_OPEN_FINISHED		12
#define DOOR_CTRL_EVENT_IDLE_TIMEOUT		13

struct door_ctrl {
	uint8_t door_id;

	struct wiegand_reader wr;
	struct event_handler hdlr;
	enum door_state state;

	uint32_t pin;

	uint16_t open_time;
	struct trigger open_trigger;

	struct trigger led_trigger;
	struct trigger buzzer_trigger;
	uint8_t buzzer_count;

	struct timer idle_timer;

	door_ctrl_check check_key;
	void *check_context;
};

int8_t door_ctrl_init(struct door_ctrl *dc,
		      const struct door_ctrl_config *cfg);

#endif /* DOOR_CONTROLLER_H */
