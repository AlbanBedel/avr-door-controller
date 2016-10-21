#include <avr/io.h>
#include "gpio.h"

volatile uint8_t * const external_irq_pc_pin[] = {
	&PINB,
	&PINC,
	&PIND,
};

const uint8_t external_irq_gpio_pc[] = {
	GPIO(B, 0),
	GPIO(B, 1),
	GPIO(B, 2),
	GPIO(B, 3),
	GPIO(B, 4),
	GPIO(B, 5),
	GPIO(B, 6),
	GPIO(B, 7),
	GPIO(C, 0),
	GPIO(C, 1),
	GPIO(C, 2),
	GPIO(C, 3),
	GPIO(C, 4),
	GPIO(C, 5),
	GPIO(C, 6),
	0, // GPIO(C, 7),
	GPIO(D, 0),
	GPIO(D, 1),
	GPIO(D, 2),
	GPIO(D, 3),
	GPIO(D, 4),
	GPIO(D, 5),
	GPIO(D, 6),
	GPIO(D, 7),
};

const uint8_t external_irq_gpio_ext[] = {
	GPIO(D, 2),
	GPIO(D, 3),
};
