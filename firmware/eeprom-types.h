#ifndef EEPROM_TYPES_H
#define EEPROM_TYPES_H

#include <stdint.h>
#include "utils.h"

#define ACCESS_TYPE_NONE		0
#define ACCESS_TYPE_PIN			1
#define ACCESS_TYPE_CARD		2
#define ACCESS_TYPE_CARD_AND_PIN	(ACCESS_TYPE_PIN | ACCESS_TYPE_CARD)

struct access_record {
	/* Pin code or card number */
	uint32_t key;
	/* Pin or key */
	uint8_t type   : 2;
	uint8_t invalid: 1;
	/* Mark if the record has been used since the last check */
	uint8_t used   : 1;
	/* Doors that can be opened with this token */
	uint8_t doors  : 4;
} PACKED;

struct door_config {
	/* Time the door should stay open in ms */
	uint16_t open_time;

	/* Start and end time for open access */
	uint16_t open_access_start_time;
	uint16_t open_access_end_time;
	/* Bitmask of the week days with open access */
	uint8_t open_access_days;

	/* Reserved */
	uint8_t reserved[];
} PACKED;

#endif /* EEPROM_TYPES_H */
