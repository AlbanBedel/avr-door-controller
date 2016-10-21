#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

struct access_record {
	/* Pin code or card number */
	uint32_t key;
	/* Pin or key */
	uint8_t pin    : 1;
	/* Allow access to admin menu */
	uint8_t admin  : 1;
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

#define ACCESS_RECORDS_SIZE \
	(EEPROM_SIZE - NUM_DOORS * sizeof(struct door_config))

#define NUM_ACCESS_RECORDS \
	(ACCESS_RECORDS_SIZE / sizeof(struct access_record))

struct eeprom_config {
	struct door_config door[NUM_DOORS];
	struct access_record access[NUM_ACCESS_RECORDS];
};

int8_t eeprom_find_access_record(uint32_t key, struct access_record *rec);

int8_t eeprom_get_door_config(uint8_t id, struct door_config *cfg);

int8_t eeprom_set_door_config(uint8_t id, const struct door_config *cfg);

#endif /* EEPROM_H */
