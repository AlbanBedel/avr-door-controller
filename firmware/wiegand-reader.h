#ifndef WIEGAND_READER_H
#define WIEGAND_READER_H

#include <stdint.h>
#include "timer.h"

struct wiegand_reader {
	uint8_t bits[5]; /* For up to 34 bits */
	uint8_t num_bits;

	uint8_t data_pins;

	struct timer word_timeout;
};

#define WIEGAND_READER_ERROR		(-1)
#define WIEGAND_READER_EVENT_KEY	0
#define WIEGAND_READER_EVENT_CARD	1

#define WIEGAND_KEY_0			0x0
#define WIEGAND_KEY_1			0x1
#define WIEGAND_KEY_2			0x2
#define WIEGAND_KEY_3			0x3
#define WIEGAND_KEY_4			0x4
#define WIEGAND_KEY_5			0x5
#define WIEGAND_KEY_6			0x6
#define WIEGAND_KEY_7			0x7
#define WIEGAND_KEY_8			0x8
#define WIEGAND_KEY_9			0x9
#define WIEGAND_KEY_A			0xA
#define WIEGAND_KEY_B			0xB

#define WIEGAND_KEY_STAR		WIEGAND_KEY_A
#define WIEGAND_KEY_ESC			WIEGAND_KEY_A

#define WIEGAND_KEY_POUND		WIEGAND_KEY_B
#define WIEGAND_KEY_ENTER		WIEGAND_KEY_B


/** Initialize the Wiegand reader object.
 *
 * To use this reader the user should register an event handler
 * on the reader.
 */
int8_t wiegand_reader_init(struct wiegand_reader *wr,
			   uint8_t d0_irq, uint8_t d1_irq);

#endif /* WIEGAND_READER_H */
