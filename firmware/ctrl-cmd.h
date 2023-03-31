#ifndef CTRL_CMD_H
#define CTRL_CMD_H

#include <stdint.h>
#include "ctrl-cmd-types.h"

int8_t ctrl_cmd_init(void);

int8_t ctrl_send_event(uint8_t type, const void *payload, uint8_t length);

void ctrl_cmd_init_device_descriptor(struct device_descriptor *desc);

#endif /* CTRL_CMD_H */
