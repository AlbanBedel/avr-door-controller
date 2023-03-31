#ifndef EEPROM_H
#define EEPROM_H

#include "eeprom-types.h"

#define ACCESS_RECORDS_SIZE \
	(EEPROM_SIZE - NUM_DOORS * sizeof(struct door_config))

#define NUM_ACCESS_RECORDS \
	(ACCESS_RECORDS_SIZE / sizeof(struct access_record))

struct eeprom_config {
	struct door_config door[NUM_DOORS];
	struct access_record_entry access[NUM_ACCESS_RECORDS];
};

uint16_t eeprom_get_free_access_record_count(void);

/* Keep the old API for now */
int8_t eeprom_get_access_record(uint16_t id, struct access_record *rec);

int8_t eeprom_set_access_record(uint16_t id, const struct access_record *rec);

int8_t eeprom_get_access(uint8_t type, uint32_t key, uint8_t *doors);

int8_t eeprom_has_access(uint8_t type, uint32_t key, uint8_t door_id);

int8_t eeprom_set_access(uint8_t type, uint32_t key, uint8_t doors);

/* New high level API */
int8_t eeprom_load_access_record(
	uint8_t type, uint32_t card, uint32_t pin,
	struct access_record_v2 *rec, uint16_t *pos);

int8_t eeprom_load_this_access_record(struct access_record_v2 *rec, uint16_t *pos);

int8_t eeprom_save_access_record(const struct access_record_v2 *rec);

void eeprom_remove_all_access(void);

/* Generic search API */
typedef int8_t (*eeprom_check_access_record_t)(
	const struct access_record_hdr *hdr, const void *check_ctx);

#define ACCESS_RECORD_ITER_START (-1)

int8_t eeprom_get_next_access_record(
	uint16_t *idx, struct access_record_v2 *rec,
	eeprom_check_access_record_t check, const void *check_ctx);

#define eeprom_for_each_access_record_where(idx, rec, check, ctx) \
	for(idx = ACCESS_RECORD_ITER_START; \
	    eeprom_get_next_access_record(&(idx), rec, check, ctx) >= 0;)

#define eeprom_for_each_access_record(idx, rec) \
	eeprom_for_each_access_record_where(idx, rec, NULL, NULL)

/* Low level API */
int8_t eeprom_read_access_record(
	uint16_t idx, struct access_record_v2 *rec);

int8_t eeprom_write_access_record(
	uint16_t idx, const struct access_record_v2 *rec);

int8_t eeprom_update_access_record_hdr(
	uint16_t idx, const struct access_record_hdr *hdr);

int8_t eeprom_get_door_config(uint8_t id, struct door_config *cfg);

int8_t eeprom_set_door_config(uint8_t id, const struct door_config *cfg);

#endif /* EEPROM_H */
