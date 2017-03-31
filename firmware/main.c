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
#include "gpio.h"

static int8_t check_key(uint8_t door_id, uint8_t type,
			uint32_t key, void *context)
{
	struct access_record rec;
	const char *result;
	int8_t err;

	err = eeprom_find_access_record(key, &rec);
	if (!err) {
		if ((rec.pin && type != DOOR_CTRL_PIN) ||
		    !(rec.doors & BIT(door_id))) {
			result = "unauthorized";
			err = -EPERM;
		} else {
			result = "authorized";
		}
	} else {
		result = "unknown";
	}

	if (DEBUG) {
		static char buffer[40];
		static const char fmt[] PROGMEM =
			"Door %d, %c %010ld -> %s\r\n";

		snprintf_P(buffer, sizeof(buffer), fmt,
			 door_id, (type == DOOR_CTRL_PIN) ? 'P' : 'C',
			 key, result);
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
	uint8_t i;

	clock_prescale_set(clock_div_1);
	timers_init();

	err = uart_init(UART_DIRECTION_BOTH, 38400, 1, UART_PARITY_NONE);
	if (!err)
		err = init_doors();

	sei();
	uart_blocking_write("Door control init ");
	if (err)
		uart_blocking_write("failed!\r\n");
	else
		uart_blocking_write("OK!\r\n");

	event_loop_run(GPIO(B, 5));

	return 0;
}
