#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <avr/eeprom.h>
#include "eeprom.h"
#include "utils.h"

static struct eeprom_config config EEMEM;

uint16_t eeprom_get_free_access_record_count(void)
{
	struct access_record rec;
	uint16_t i, count = 0;

	for (i = 0; i < ARRAY_SIZE(config.access); i++) {
		eeprom_read_block(&rec, &config.access[i], sizeof(rec));
		if (rec.invalid || rec.type == ACCESS_TYPE_NONE)
			count++;
	}

	return count;
}

int8_t eeprom_get_access_record(uint16_t id, struct access_record *rec)
{
	if (id >= ARRAY_SIZE(config.access))
		return -ENOENT;

	eeprom_read_block(rec, &config.access[id], sizeof(*rec));
	/* Normalize invalid records to an empty one */
	if (rec->invalid)
		memset(rec, 0, sizeof(*rec));

	return 0;
}

int8_t eeprom_set_access_record(uint16_t id, const struct access_record *rec)
{
	if (id >= ARRAY_SIZE(config.access))
		return -ENOENT;

	eeprom_write_block(rec, &config.access[id], sizeof(*rec));
	return 0;
}

static int8_t eeprom_find_access_record(uint8_t type, uint32_t key,
					struct access_record *rec,
					uint16_t *index)
{
	uint16_t i;

	for (i = 0; i < ARRAY_SIZE(config.access); i++) {
		eeprom_get_access_record(i, rec);
		if (rec->type != type)
			continue;
		if (type != ACCESS_TYPE_NONE && rec->key != key)
			continue;
		/* found */
		if (index)
			*index = i;
		return 0;
	}

	return -ENOENT;
}

int8_t eeprom_get_access(uint8_t type, uint32_t key, uint8_t *doors)
{
	struct access_record rec;
	int8_t err;

	err = eeprom_find_access_record(type, key, &rec, NULL);
	if (err < 0)
		return err;

	if (doors)
		*doors = rec.doors;

	return 0;
}

int8_t eeprom_has_access(uint8_t type, uint32_t key, uint8_t door_id)
{
	struct access_record rec;
	uint16_t index;
	int8_t err;

	/* Lookup the access record */
	err = eeprom_find_access_record(type, key, &rec, &index);
	if (err < 0)
		return err;

	/* Check if this record allow to open the door */
	if (!(rec.doors & BIT(door_id)))
		return -EPERM;

	/* Mark the record as used */
	if (!rec.used) {
		rec.used = 1;
		eeprom_set_access_record(index, &rec);
	}

	return 0;
}

int8_t eeprom_set_access(uint8_t type, uint32_t key, uint8_t doors)
{
	struct access_record rec;
	uint16_t index;
	int8_t err;

	if (type == ACCESS_TYPE_NONE)
		return -EINVAL;

	err = eeprom_find_access_record(type, key, &rec, &index);
	if (err < 0) { /* No record found */
		/* If removing access nothing has to be done */
		if (doors == 0)
			return 0;

		/* Find a free record */
		err = eeprom_find_access_record(
			ACCESS_TYPE_NONE, 0, &rec, &index);
		if (err < 0)
			return -ENOSPC;

		rec.invalid = 0;
		rec.type = type;
		rec.key = key;
	}

	/* Set the accessable doors */
	rec.doors = doors;

	/* If no door is left remove the whole record */
	if (doors == 0) {
		rec.type = ACCESS_TYPE_NONE;
		rec.key  = 0;
	}

	return eeprom_set_access_record(index, &rec);
}

void eeprom_remove_all_access(void)
{
	struct access_record rec;
	uint16_t i;

	for (i = 0; i < ARRAY_SIZE(config.access); i++) {
		eeprom_read_block(&rec, &config.access[i], sizeof(rec));
		if (rec.invalid || rec.type == ACCESS_TYPE_NONE)
			continue;

		rec.type = ACCESS_TYPE_NONE;
		eeprom_write_block(&rec, &config.access[i], sizeof(rec));
	}
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
