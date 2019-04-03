#include <stdlib.h>
#include <errno.h>
#include <util/atomic.h>

#include "work-queue.h"
#include "utils.h"
#include "sleep.h"
#include "timer.h"
#include "gpio.h"

struct work {
	struct work * volatile next;

	struct worker *worker;
	uint8_t cmd;
	union work_arg arg;
};

#define MAX_PENDING_WORKS 8

static struct work * volatile runq_head;
static struct work * volatile runq_tail;
static struct work wq_storage[MAX_PENDING_WORKS];

static uint8_t life_gpio;

int8_t work_queue_schedule(struct worker *worker,
			   uint8_t cmd, union work_arg arg)
{
	struct work *work = NULL;
	uint8_t i;

	if (!worker)
		return -EINVAL;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		/* Find a free event in the storage */
		for (i = 0; i < ARRAY_SIZE(wq_storage); i++)
			if (wq_storage[i].worker == NULL) {
				work = &wq_storage[i];
				break;
			}
		if (work) {
			/* Fill the event */
			work->next = NULL;
			work->worker = worker;
			work->cmd = cmd;
			work->arg = arg;

			/* Add it to the tail */
			if (runq_tail)
				runq_tail->next = work;
			else
				runq_head = work;
			runq_tail = work;
		}
	}

	return work ? 0 : -ENOMEM;
}

int8_t work_queue_deschedule(const struct worker *worker, uint8_t cmd)
{
	struct work *work, *prev, *next;

	if (!worker)
		return -EINVAL;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		for (work = runq_head, prev = NULL; work ; work = next) {
			next = work->next;
			if (work->worker != worker || work->cmd != cmd) {
				prev = work;
				continue;
			}
			if (prev)
				prev->next = next;
			else
				runq_head = next;
			if (!next)
				runq_tail = prev;
			work->next = NULL;
			work->worker = NULL;
		}
	}

	return -ENOENT;
}

static void work_queue_run_once(void)
{
	struct work *work;

	ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
		/* Get the run queue head */
		if ((work = runq_head)) {
			/* Remove it from the queue */
			runq_head = work->next;
			if (!work->next)
				runq_tail = NULL;
			work->next = NULL;
		}
	}

	/* Run the worker */
	if (work) {
		/* Get the context on the stack */
		struct worker *worker = work->worker;
		uint8_t cmd = work->cmd;
		union work_arg arg = work->arg;

		/* Free the work slot */
		work->worker = NULL;

		/* Run the worker */
		worker->execute(worker, cmd, arg);
	}
}

void work_queue_run(uint8_t gpio)
{
	life_gpio = gpio;
	gpio_direction_output(life_gpio, 1);
	while (1) {
		work_queue_run_once();
		/* Sleep if no event is pending */
		sleep_if(!runq_head);
	}
	gpio_set_value(life_gpio, 0);
}
