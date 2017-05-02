#ifndef EEPROM_TYPES_H
#define EEPROM_TYPES_H

#include <stdint.h>

#define ACCESS_TYPE_NONE		0
#define ACCESS_TYPE_PIN			1
#define ACCESS_TYPE_CARD		2
#define ACCESS_TYPE_CARD_AND_PIN	(ACCESS_TYPE_PIN | ACCESS_TYPE_CARD)

struct access_record {
	/* Pin code or card number */
	uint32_t key;
	/* Pin or key */
	uint8_t type   : 2;
	uint8_t resvd  : 2;
	/* Doors that can be opened with this token */
	uint8_t doors  : 4;
};

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
};

#endif /* EEPROM_TYPES_H */
