#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <avr/pgmspace.h>
#include "gpio.h"
#include "timer.h"
#include "utils.h"
#include "door-controller.h"

/* This gives us 10 attempts before we reach the max of 30s */
#define BLOCK_PER_ATTEMPT_DURATION  3000

#define BUZZER_KEY_DURATION         100
#define BUZZER_ERROR_DURATION	    400
#define BUZZER_ACCEPTED_DURATION    100
#define BUZZER_REJECTED_DURATION    400

static const uint16_t buzzer_rejected_seq[] = {
	0, 200, 600, 200, 600, 200, 600
};

static const uint16_t buzzer_accepted_seq[] = {
	0, 100, 200 /*, 100, 200*/
};

#if DEBUG
#include <stdio.h>
#include "uart.h"

static const char *state_names[] = {
	"IDLE",
	"READ PIN",
	"OPENING",
	"REJECT",
	"ERROR",
};

static char print_buf[32];

static const char *state_name(enum door_state state)
{
	if (state < 0 || state >= ARRAY_SIZE(state_names))
		return "";
	else
		return state_names[state];
}

static void door_ctrl_show_state(uint8_t event, union event_val val,
				 void *context)
{
	static const char fmt[] PROGMEM = "[%d]-> %lx (%s)\r\n";
	struct door_ctrl *dc = context;

	snprintf_P(print_buf, sizeof(print_buf), fmt,
		   dc->door_id, val.u, state_name(val.u));
	uart_blocking_write(print_buf);
}
#endif

static void door_ctrl_set_state(struct door_ctrl *dc, enum door_state state)
{
	dc->state = state;
#if DEBUG
	event_add(dc, DOOR_CTRL_EVENT_STATE_CHANGED,
		  EVENT_VAL((uint32_t)state));
#endif
}

void idle_on_timeout(void *context)
{
	struct door_ctrl *dc = context;

	door_ctrl_set_state(dc, DOOR_CTRL_IDLE);
}

void on_buzzer_finished(void *context)
{
	struct door_ctrl *dc = context;

	switch (dc->state) {
	case DOOR_CTRL_REJECTED:
		door_ctrl_set_state(dc, DOOR_CTRL_IDLE);
	}
}

static int8_t door_ctrl_check_key(struct door_ctrl *dc,
				  uint8_t type, uint32_t key)
{
	if (!dc->check_key)
		return -ENOENT;

	return dc->check_key(dc->door_id, type, key, dc->check_context);
}

static void door_ctrl_open(struct door_ctrl *dc)
{
	door_ctrl_set_state(dc, DOOR_CTRL_OPENING);
	trigger_start(&dc->open_trigger, dc->open_time);
	trigger_start(&dc->led_trigger, dc->open_time);
	trigger_start_seq(&dc->buzzer_trigger, buzzer_accepted_seq,
		      ARRAY_SIZE(buzzer_accepted_seq));

	dc->reject_count = 0;
}

static void door_ctrl_reject(struct door_ctrl *dc)
{
	uint16_t block_time;

	door_ctrl_set_state(dc, DOOR_CTRL_REJECTED);
	trigger_start_seq(&dc->buzzer_trigger, buzzer_rejected_seq,
			  ARRAY_SIZE(buzzer_rejected_seq));
}

static void door_ctrl_error(struct door_ctrl *dc)
{
	door_ctrl_set_state(dc, DOOR_CTRL_IDLE);
	trigger_start(&dc->buzzer_trigger, BUZZER_ERROR_DURATION);
}

static void on_wiegand_event(uint8_t event, union event_val val, void *context)
{
	struct door_ctrl *dc = context;

#if DEBUG
	{
		static const char fmt[] PROGMEM =
			"[%d] WG event %d = %ld\r\n";
		snprintf_P(print_buf, sizeof(print_buf), fmt,
			 dc->state, event, val.u);
		uart_blocking_write(print_buf);
	}
#endif

	/* TODO: Handle protocol errors */
	if (event == WIEGAND_READER_ERROR) {
		door_ctrl_set_state(dc, DOOR_CTRL_IDLE);
		return;
	}

	switch (dc->state) {
	case DOOR_CTRL_IDLE:
		switch (event) {
		case WIEGAND_READER_EVENT_KEY:
			if (val.u == WIEGAND_KEY_ENTER) {
				door_ctrl_error(dc);
				break;
			}
			/* The PIN is stored with one key per nibble,
			 * to allow detecting missing leading zeros we fill
			 * the "unused bits" with 1'. */
			dc->pin = (-1 & ~0xF) | (val.u & 0xF);
			door_ctrl_set_state(dc, DOOR_CTRL_READING_PIN);
			break;
		case WIEGAND_READER_EVENT_CARD:
			if (door_ctrl_check_key(
				    dc, DOOR_CTRL_CARD, val.u) == 0)
				door_ctrl_open(dc);
			else
				door_ctrl_reject(dc);
			break;
		default:
			door_ctrl_error(dc);
			break;
		}
		break;
	case DOOR_CTRL_READING_PIN:
		if (event != WIEGAND_READER_EVENT_KEY) {
			door_ctrl_error(dc);
			break;
		}
		if (val.u == WIEGAND_KEY_ENTER) {
			if (door_ctrl_check_key(
				    dc, DOOR_CTRL_PIN, dc->pin) == 0)
				door_ctrl_open(dc);
			else
				door_ctrl_reject(dc);
			dc->pin = 0;
		} else {
			dc->pin <<= 4;
			dc->pin = (dc->pin & ~0xF) | (val.u & 0xF);
		}
		break;
	case DOOR_CTRL_OPENING:
	case DOOR_CTRL_REJECTED:
		/* Ignore events while we open the door */
		break;
	case DOOR_CTRL_ERROR:
		door_ctrl_set_state(dc, DOOR_CTRL_IDLE);
		break;
	}
}

// TODO: Move config to program space
int8_t door_ctrl_init(struct door_ctrl *dc,
		      const struct door_ctrl_config *cfg)
{
	int8_t err;

	if (!dc || !cfg)
		return -EINVAL;

	memset(dc, 0, sizeof(*dc));

	dc->door_id = cfg->door_id;
	dc->open_time = cfg->open_time;
	dc->check_key = cfg->check_key;
	dc->check_context = cfg->check_context;

	dc->wr_hdlr.source = &dc->wr;
	dc->wr_hdlr.handler = on_wiegand_event;
	dc->wr_hdlr.context = dc;

	timer_init(&dc->reject_timer, idle_on_timeout, dc);

	err = event_handler_add(&dc->wr_hdlr);
	if (err)
		return err;

	err = wiegand_reader_init(&dc->wr, cfg->d0_irq, cfg->d1_irq);
	if (err)
		return err;

	err = trigger_init(&dc->open_trigger, cfg->open_gpio,
			   cfg->open_low_active, idle_on_timeout, dc);
	if (err)
		return err;

	err = trigger_init(&dc->led_trigger, cfg->led_gpio,
			   cfg->led_low_active, NULL, NULL);
	if (err)
		return err;

	err = trigger_init(&dc->buzzer_trigger, cfg->buzzer_gpio,
			   cfg->buzzer_low_active, on_buzzer_finished, dc);
	if (err)
		return err;

#if DEBUG
	dc->state_change_hdlr.source = dc;
	dc->state_change_hdlr.handler =	door_ctrl_show_state;
	dc->state_change_hdlr.context = dc;
	event_handler_add(&dc->state_change_hdlr);
#endif

	return 0;
}
