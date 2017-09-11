#include <avr/pgmspace.h>
#include "door-controller.h"
#include "external-irq.h"
#include "gpio.h"

const struct door_ctrl_config doors_config[] PROGMEM = {
	{
		.door_id = 0,
		.d0_irq = IRQ(PC, 4),
		.d1_irq = IRQ(PC, 3),
		.open_gpio = GPIO(C, 1, HIGH_ACTIVE),
		.open_time = 4000,
		.led_gpio = GPIO(B, 1, HIGH_ACTIVE),
		.buzzer_gpio = GPIO(B, 0, HIGH_ACTIVE),
		.status_gpio = GPIO(B, 2, LOW_ACTIVE),
		.status_pull = 1,
		.open_btn_gpio = GPIO(D, 7, LOW_ACTIVE),
		.open_btn_pull = 1,
	},
	{
		.door_id = 1,
		.d0_irq = IRQ(PC, 22),
		.d1_irq = IRQ(PC, 21),
		.open_gpio = GPIO(C, 2, HIGH_ACTIVE),
		.open_time = 4000,
		.led_gpio = GPIO(D, 3, HIGH_ACTIVE),
		.buzzer_gpio = GPIO(D, 2, HIGH_ACTIVE),
		.status_gpio = GPIO(D, 4, LOW_ACTIVE),
		.status_pull = 1,
		.open_btn_gpio = GPIO(C, 0, LOW_ACTIVE),
		.open_btn_pull = 1,
	}
};
