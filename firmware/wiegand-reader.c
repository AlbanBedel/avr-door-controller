#include <string.h>
#include <errno.h>
#include "wiegand-reader.h"
#include "external-irq.h"
#include "event-queue.h"
#include "gpio.h"

/* Timeout to trigger reading the bits */
#define WORD_TIMEOUT 10

static inline uint8_t get_bit(uint8_t *data, uint8_t idx)
{
	return (data[idx >> 3] >> (idx & 7)) & 1;
}

static inline void set_bit(uint8_t *data, uint8_t idx, uint8_t val)
{
	if (val)
		data[idx >> 3] |= 1 << (idx & 7);
	else
		data[idx >> 3] &= ~(1 << (idx & 7));
}

static uint8_t odd_parity(uint8_t *data, uint8_t from, uint8_t to)
{
	uint8_t p;

	for (p = 0; from <= to; from++)
		p += get_bit(data, from);

	return p & 1;
}

static uint8_t even_parity(uint8_t *data, uint8_t from, uint8_t to)
{
	return !odd_parity(data, from, to);
}

static void wiegand_reader_event(struct wiegand_reader *wr,
				    uint8_t event, uint32_t val)
{
	event_add(wr, event, EVENT_VAL(val));
}

static int8_t wiegand_reader_process_4bits_code(struct wiegand_reader *wr)
{
	uint8_t key = 0;
	uint8_t i;

	/* Reverse the bits order */
	for (i = 0; i < 4; i++) {
		key <<= 1;
		if (get_bit(&wr->bits[0], i))
			key |= 1;

	}

	if (key > WIEGAND_KEY_B)
		return -EINVAL;

	wiegand_reader_event(wr, WIEGAND_READER_EVENT_KEY, key);
	return 0;
}

static int8_t wiegand_reader_process_8bits_code(struct wiegand_reader *wr)
{
	/* Check the data validity */
	if ((wr->bits[0] & 0xF) != ~(wr->bits[0] >> 4))
		return -EINVAL;

	return wiegand_reader_process_4bits_code(wr);
}

static int8_t wiegand_reader_process_26bits_code(struct wiegand_reader *wr)
{
	uint32_t card;
	uint8_t parity;
	uint8_t i;

	/* Check the parity */
	parity = even_parity(wr->bits, 1, 8);
	if (parity != get_bit(wr->bits, 0))
		return -EINVAL;

	parity = odd_parity(wr->bits, 9, 24);
	if (parity != get_bit(wr->bits, 25))
		return -EINVAL;

	/* Read out the code */
	for (card = 0, i = 1; i <= 24; i++) {
		card <<= 1;
		if (get_bit(wr->bits, i))
			card |= 1;
	}

	wiegand_reader_event(wr, WIEGAND_READER_EVENT_CARD, card);
	return 0;
}

static int8_t wiegand_reader_process_34bits_code(struct wiegand_reader *wr)
{
	// TODO
	return -EINVAL;
}

static void wiegand_reader_on_word_timeout(void *context)
{
	struct wiegand_reader *wr = context;
	int err = -EINVAL;

	switch(wr->num_bits) {
	case 4:
		err = wiegand_reader_process_4bits_code(wr);
		break;
	case 8:
		err = wiegand_reader_process_8bits_code(wr);
		break;
	case 26:
		err = wiegand_reader_process_26bits_code(wr);
		break;
	case 34:
		err = wiegand_reader_process_34bits_code(wr);
		break;
	}

	wr->num_bits = 0;
	if (err)
		wiegand_reader_event(wr, WIEGAND_READER_ERROR, err);
}

void wiegand_reader_data_pin_changed(struct wiegand_reader *wr,
				    uint8_t pin, uint8_t state)
{

	set_bit(&wr->data_pins, pin, state);

	switch (wr->data_pins & 3) {
	case 0: /* No reader */
		wr->num_bits = 0;
		timer_deschedule(&wr->word_timeout);
		wiegand_reader_event(wr, WIEGAND_READER_ERROR, -ENODEV);
		return;
	case 1: /* 1 bit */
		if (wr->num_bits < sizeof(wr->bits) * 8)
			set_bit(wr->bits, wr->num_bits, 1);
		return;
	case 2: /* 0 bit */
		if (wr->num_bits < sizeof(wr->bits) * 8)
			set_bit(wr->bits, wr->num_bits, 0);
		return;
	case 3: /* Inter bit */
		wr->num_bits++;
		timer_schedule_in(&wr->word_timeout, WORD_TIMEOUT);
		return;
	}
}

static void wiegand_reader_d0(uint8_t pin_state, void *context)
{
	struct wiegand_reader *wr = context;

	wiegand_reader_data_pin_changed(wr, 0, pin_state);
}

static void wiegand_reader_d1(uint8_t pin_state, void *context)
{
	struct wiegand_reader *wr = context;

	wiegand_reader_data_pin_changed(wr, 1, pin_state);
}

int8_t wiegand_reader_init(struct wiegand_reader *wr,
			   uint8_t d0_irq, uint8_t d1_irq)
{
	int8_t err;

	memset(wr, 0, sizeof(*wr));

	err = external_irq_setup(d0_irq, 1, IRQ_TRIGGER_BOTH_EDGE,
				 wiegand_reader_d0, wr);
	if (err)
		return err;

	err = external_irq_setup(d1_irq, 1, IRQ_TRIGGER_BOTH_EDGE,
				 wiegand_reader_d1, wr);
	if (err)
		return err;

	timer_init(&wr->word_timeout, wiegand_reader_on_word_timeout, wr);

	set_bit(&wr->data_pins, 0, gpio_get_value(
			external_irq_get_gpio(d0_irq)));
	set_bit(&wr->data_pins, 1, gpio_get_value(
			external_irq_get_gpio(d1_irq)));
	external_irq_unmask(d0_irq);
	external_irq_unmask(d1_irq);

	return 0;
}
