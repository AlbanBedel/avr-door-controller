#ifndef COMPLETION_H
#define COMPLETION_H

#include "sleep.h"

struct completion {
	volatile uint8_t done;
};

static inline void completion_done(struct completion *cmp)
{
	cmp->done = 1;
}

static inline void completion_done_cb(void *cmp)
{
	return completion_done((struct completion *)cmp);
}

static inline void completion_wait(struct completion *cmp)
{
	sleep_until(cmp->done);
}

#endif /* COMPLETION_H */
