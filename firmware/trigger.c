#include <string.h>
#include <errno.h>
#include "trigger.h"
#include "gpio.h"

static void trigger_on_timeout(void *context)
{
	struct trigger *tr = context;

	while (tr->seq_pos < tr->seq_len && !tr->seq[tr->seq_pos])
		tr->seq_pos++;

	if (tr->seq_pos >= tr->seq_len) {
		trigger_stop(tr);
		if (tr->on_finished)
			tr->on_finished(tr->on_finished_context);
		return;
	}

	/* Play the next step */
	if (tr->gpio)
		gpio_set_value(tr->gpio, !(tr->seq_pos & 1));
	timer_schedule_in(&tr->timer, tr->seq[tr->seq_pos]);
	tr->seq_pos++;
}

int8_t trigger_start_seq(struct trigger *tr, const uint16_t *seq,
		       uint8_t seq_len)
{
	if (tr->seq)
		return -EBUSY;
	if (!seq || seq_len < 1 || seq_len == -1)
		return -EINVAL;

	tr->seq = seq;
	tr->seq_len = seq_len;
	tr->seq_pos = 0;
	trigger_on_timeout(tr);
	return 0;
}

void trigger_start(struct trigger *tr, uint16_t duration)
{
	tr->single_seq = duration;
	trigger_start_seq(tr, &tr->single_seq, 1);
}

void trigger_stop(struct trigger *tr)
{
	if (tr->gpio)
		gpio_set_value(tr->gpio, 0);
	tr->seq = NULL;
	tr->seq_len = 0;
	tr->seq_pos = 0;
}

int8_t trigger_init(struct trigger *tr, uint8_t gpio,
		    timer_cb_t on_finished, void *on_finished_context)
{
	if (!tr || (gpio && !gpio_is_valid(gpio)))
		return -EINVAL;

	memset(tr, 0, sizeof(*tr));
	tr->gpio = gpio;
	tr->on_finished = on_finished;
	tr->on_finished_context = on_finished_context;

	timer_init(&tr->timer, trigger_on_timeout, tr);

	if (gpio)
		return gpio_direction_output(gpio, 0);
	return 0;
}
