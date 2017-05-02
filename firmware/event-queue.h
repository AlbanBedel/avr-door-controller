#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>

union event_val {
	char c;
	uint32_t u;
	int32_t i;
	void *data;
};

#define EVENT_VAL(v) ((union event_val)(v))
#define EVENT_CHAR(c) EVENT_VAL((char)(c))
#define EVENT_INT(i)  EVENT_VAL((int32_t)(i))
#define EVENT_UINT(i) EVENT_VAL((uint32_t)(i))
#define EVENT_PTR(p)  EVENT_VAL((void *)(p))

typedef void (*event_handler_cb)(
	uint8_t event, union event_val val, void *context);

struct event_handler {
	struct event_handler *next;

	const void *source;

	uint8_t id;
	uint8_t mask;

	event_handler_cb handler;
	void *context;
};

int8_t event_handler_add(struct event_handler *hdlr);

int8_t event_handler_remove(struct event_handler *hdlr);

int8_t event_add(const void *source, uint8_t id, union event_val val);

int8_t event_remove(const void *source, uint8_t id);

void event_loop_run(uint8_t life_gpio);

#endif /* EVENT_QUEUE_H */
