#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <avr/pgmspace.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <avr/boot.h>
#include <util/delay.h>
#include "event-queue.h"
#include "door-controller.h"
#include "external-irq.h"
#include "eeprom.h"
#include "utils.h"
#include "uart.h"
#include "ctrl-cmd.h"
#include "gpio.h"

static int8_t check_key(uint8_t door_id, uint8_t type,
			uint32_t key, void *context)
{
	int8_t err;

	err = eeprom_has_access(type, key, door_id);
	if (DEBUG) {
		static char buffer[40];
		static const char fmt[] PROGMEM =
			"Door %d, %c %010ld -> %sauthorized\r\n";

		snprintf_P(buffer, sizeof(buffer), fmt,
			   door_id, (type == DOOR_CTRL_PIN) ? 'P' : 'C',
			   key, err ? "un" :"");
		uart_blocking_write(buffer);
	}

	return err;
}

extern const struct door_ctrl_config doors_config[] PROGMEM;
static struct door_ctrl dc[NUM_DOORS];

static int init_doors(void)
{
	int8_t err;
	uint8_t i;

	for (i = 0; i < NUM_DOORS; i++) {
		struct door_config eeprom_cfg;
		struct door_ctrl_config cfg;

		memcpy_P(&cfg, &doors_config[i], sizeof(cfg));
		cfg.check_key = check_key;

		eeprom_get_door_config(i, &eeprom_cfg);
		if (eeprom_cfg.open_time > 0 &&
		    eeprom_cfg.open_time < INT16_MAX / 2)
			cfg.open_time = eeprom_cfg.open_time;

		err = door_ctrl_init(&dc[i], &cfg);
		if (err)
			break;
	}

	return err;
}

int main(void)
{
	int8_t err;

	clock_prescale_set(clock_div_1);
	timers_init();

	err = ctrl_cmd_init();
	if (!err)
		err = init_doors();

	sei();
	ctrl_send_event(CTRL_EVENT_STARTED, NULL, 0);
	event_loop_run(GPIO(B, 5));

	return 0;
}
