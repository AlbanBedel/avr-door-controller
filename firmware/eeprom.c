#include <stdlib.h>
#include <errno.h>
#include <avr/eeprom.h>
#include "eeprom.h"
#include "utils.h"

static struct eeprom_config config EEMEM;

int8_t eeprom_find_access_record(uint32_t key, struct access_record *rec)
{
	uint8_t i;

	for (i = 0; i < ARRAY_SIZE(config.access); i++) {
		rec->key = eeprom_read_dword(&config.access[i].key);
		if (rec->key == key) {
			eeprom_read_block(rec, &config.access[i],
					  sizeof(*rec));
			return 0;
		}
	}

	return -ENOENT;
}

int8_t eeprom_get_door_config(uint8_t id, struct door_config *cfg)
{
	if (id >= ARRAY_SIZE(config.door))
		return -EINVAL;

	eeprom_read_block(cfg, &config.door[id], sizeof(*cfg));
	return 0;
}

int8_t eeprom_set_door_config(uint8_t id, const struct door_config *cfg)
{
	if (id >= ARRAY_SIZE(config.door))
		return -EINVAL;

	eeprom_write_block(cfg, &config.door[id], sizeof(*cfg));
	return 0;
}
