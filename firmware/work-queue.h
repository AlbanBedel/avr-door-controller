#ifndef WORK_QUEUE
#define WORK_QUEUE

union work_arg {
	char c;
	uint32_t u;
	int32_t i;
	void *data;
};

#define WORK_ARG(v) ((union work_arg)(v))
#define WORK_ARG_CHAR(c) WORK_ARG((char)(c))
#define WORK_ARG_INT(i)  WORK_ARG((int32_t)(i))
#define WORK_ARG_UINT(i) WORK_ARG((uint32_t)(i))
#define WORK_ARG_PTR(p)  WORK_ARG((void *)(p))

struct worker {
	void (*execute)(struct worker *worker,
			uint8_t cmd, union work_arg arg);
};

int8_t work_queue_schedule(struct worker *worker,
			   uint8_t cmd, union work_arg arg);

int8_t work_queue_deschedule(const struct worker *worker, uint8_t cmd);

void work_queue_run(uint8_t gpio);

#endif /* WORK_QUEUE */
