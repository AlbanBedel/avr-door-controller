#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdint.h>
#include "timer.h"
#include "work-queue.h"

struct trigger {
	uint8_t gpio;

	struct timer timer;

	uint16_t single_seq;
	const uint16_t *seq;
	uint8_t seq_len;
	uint8_t seq_pos;

	struct worker *on_finished;
	uint8_t on_finished_cmd;
};

int8_t trigger_init(struct trigger *tr, uint8_t gpio,
		    struct worker *on_finished, uint8_t on_finished_cmd);

void trigger_set(struct trigger *tr, uint8_t value);

void trigger_start(struct trigger *tr, uint16_t duration);

int8_t trigger_start_seq(struct trigger *tr, const uint16_t *seq,
			 uint8_t seq_len);

void trigger_stop(struct trigger *tr);

#endif /* TRIGGER_H */
