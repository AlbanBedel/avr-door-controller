#ifndef CTRL_CMD_H
#define CTRL_CMD_H

#include <stdint.h>
#include "ctrl-cmd-types.h"

int8_t ctrl_cmd_init(void);

int8_t ctrl_send_event(uint8_t type, const void *payload, uint8_t length);

#endif /* CTRL_CMD_H */
