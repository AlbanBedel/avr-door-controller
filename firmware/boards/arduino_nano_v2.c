#include <avr/pgmspace.h>
#include "door-controller.h"
#include "external-irq.h"
#include "gpio.h"

const struct door_ctrl_config doors_config[] PROGMEM = {
	{
		.door_id = 0,
		.d0_irq = IRQ(PC, 4),
		.d1_irq = IRQ(PC, 3),
		.open_gpio = GPIO(C, 0),
		.open_time = 4000,
		.led_gpio = GPIO(B, 1),
		.led_low_active = 1,
		.buzzer_gpio = GPIO(B, 0),
		.buzzer_low_active = 1,
	},
	{
		.door_id = 1,
		.d0_irq = IRQ(PC, 23),
		.d1_irq = IRQ(PC, 22),
		.open_gpio = GPIO(C, 1),
		.open_time = 4000,
		.led_gpio = GPIO(D, 3),
		.led_low_active = 1,
		.buzzer_gpio = GPIO(D, 2),
		.buzzer_low_active = 1,
	}
};
