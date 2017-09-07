#include <avr/io.h>
#include "gpio.h"

volatile uint8_t * const external_irq_pc_pin[] = {
	&PINB,
	&PINC,
	&PIND,
};

const uint8_t external_irq_gpio_pc[] = {
	GPIO(B, 0, HIGH_ACTIVE),
	GPIO(B, 1, HIGH_ACTIVE),
	GPIO(B, 2, HIGH_ACTIVE),
	GPIO(B, 3, HIGH_ACTIVE),
	GPIO(B, 4, HIGH_ACTIVE),
	GPIO(B, 5, HIGH_ACTIVE),
	GPIO(B, 6, HIGH_ACTIVE),
	GPIO(B, 7, HIGH_ACTIVE),
	GPIO(C, 0, HIGH_ACTIVE),
	GPIO(C, 1, HIGH_ACTIVE),
	GPIO(C, 2, HIGH_ACTIVE),
	GPIO(C, 3, HIGH_ACTIVE),
	GPIO(C, 4, HIGH_ACTIVE),
	GPIO(C, 5, HIGH_ACTIVE),
	GPIO(C, 6, HIGH_ACTIVE),
	0, // GPIO(C, 7, HIGH_ACTIVE),
	GPIO(D, 0, HIGH_ACTIVE),
	GPIO(D, 1, HIGH_ACTIVE),
	GPIO(D, 2, HIGH_ACTIVE),
	GPIO(D, 3, HIGH_ACTIVE),
	GPIO(D, 4, HIGH_ACTIVE),
	GPIO(D, 5, HIGH_ACTIVE),
	GPIO(D, 6, HIGH_ACTIVE),
	GPIO(D, 7, HIGH_ACTIVE),
};

const uint8_t external_irq_gpio_ext[] = {
	GPIO(D, 2, HIGH_ACTIVE),
	GPIO(D, 3, HIGH_ACTIVE),
};
