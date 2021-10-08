#include <stdlib.h>
#include <errno.h>
#include "eeprom.h"

static int8_t access_record_v1_from_v2(
	struct access_record *rec_v1, const struct access_record_v2 *rec_v2)
{
	rec_v1->key = 0;
	rec_v1->type = 0;
	rec_v1->invalid = 0;
	rec_v1->used = rec_v2->hdr.used;
	rec_v1->doors = rec_v2->hdr.doors;

	if (ACCESS_RECORD_HAS_PIN(rec_v2)) {
		/* Return an error if this record can't be represented
		 * with the old data format */
		if (ACCESS_RECORD_PIN_TYPE(rec_v2) !=
		    ACCESS_RECORD_TYPE_PIN_FIXED)
			return -EEXIST;

		rec_v1->type |= ACCESS_TYPE_PIN;
		rec_v1->key ^= rec_v2->pin.fixed;
	}

	if (ACCESS_RECORD_HAS_CARD(rec_v2)) {
		/* Return an error if this record can't be represented
		 * with the old data format */
		if (ACCESS_RECORD_CARD_TYPE(rec_v2) !=
		    ACCESS_RECORD_TYPE_CARD_ID)
			return -EEXIST;

		rec_v1->type |= ACCESS_TYPE_CARD;
		rec_v1->key ^= rec_v2->card;
	}

	return 0;
}

static int8_t access_record_v2_from_v1(
	struct access_record_v2 *rec_v2, const struct access_record *rec_v1)
{
	/* We can't convert old style card & pin record to the new style */
	if (rec_v1->type == ACCESS_TYPE_CARD_AND_PIN)
		return -EBADF;

	rec_v2->hdr.type = ACCESS_RECORD_TYPE(NONE, NONE);
	rec_v2->hdr.used = rec_v1->used;
	rec_v2->hdr.doors = rec_v1->doors;

	if (rec_v1->type == ACCESS_TYPE_PIN) {
		rec_v2->hdr.type = ACCESS_RECORD_TYPE(NONE, FIXED);
		rec_v2->pin.fixed = rec_v1->key;
	} else {
		rec_v2->pin.fixed = 0;
	}

	if (rec_v1->type == ACCESS_TYPE_CARD) {
		rec_v2->hdr.type = ACCESS_RECORD_TYPE(ID, NONE);
		rec_v2->card = rec_v1->key;
	} else {
		rec_v2->card = 0;
	}

	return 0;
}

int8_t eeprom_get_access_record(uint16_t id, struct access_record *rec_v1)
{
	struct access_record_v2 rec_v2;
	int8_t err;

	err = eeprom_read_access_record(id, &rec_v2);
	if (err) {
		/* In the new API empty records return -ENOENT,
		 * in the old API we return an actual empty record */
		if (err == -ENOENT) {
			*rec_v1 = (struct access_record){};
			return 0;
		}
		/* In the new API out of bound access return -EINVAL,
		 * in the old API we return -ENOENT */
		if (err == -EINVAL)
			return -ENOENT;
		/* Pass the rest through */
		return err;
	}

	return access_record_v1_from_v2(rec_v1, &rec_v2);
}

int8_t eeprom_set_access_record(uint16_t id, const struct access_record *rec_v1)
{
	struct access_record_v2 rec_v2;
	int8_t err;

	err = access_record_v2_from_v1(&rec_v2, rec_v1);
	if (err)
		return err;

	err = eeprom_write_access_record(id, &rec_v2);
	/* In the new API out of bound access return -EINVAL,
	 * in the old API we return -ENOENT */
	if (err == -EINVAL)
		return -ENOENT;
	return err;
}

int8_t eeprom_get_access(uint8_t type, uint32_t key, uint8_t *doors)
{
	struct access_record_v2 rec_v2;
	struct access_record rec_v1 = {
		.type = type,
		.key = key,
	};
	int8_t err;

	err = access_record_v2_from_v1(&rec_v2, &rec_v1);
	if (err)
		return err;

	err = eeprom_load_this_access_record(&rec_v2, NULL);
	if (err)
		return err;

	if (doors)
		*doors = rec_v2.hdr.doors;

	return 0;
}

int8_t eeprom_set_access(uint8_t type, uint32_t key, uint8_t doors)
{
	struct access_record_v2 rec_v2;
	struct access_record rec_v1 = {
		.type = type,
		.key = key,
	};
	int8_t err;

	err = access_record_v2_from_v1(&rec_v2, &rec_v1);
	if (err)
		return err;

	return eeprom_save_access_record(&rec_v2);
}

struct access_record_match {
	uint8_t type;
	uint8_t doors;
};

static int8_t access_record_filter(
	const struct access_record_hdr *hdr, const void *val)
{
	const struct access_record_match *match = val;

	/* Match on the door */
	if ((hdr->doors & match->doors) == 0)
		return 0;

	/* Then on the type of data in the record */
	switch(match->type) {
	case ACCESS_TYPE_PIN:
		return !ACCESS_RECORD_TYPE_HAS_CARD(hdr->type) &&
			ACCESS_RECORD_TYPE_PIN(hdr->type) == ACCESS_RECORD_TYPE_PIN_FIXED;

	case ACCESS_TYPE_CARD:
		return !ACCESS_RECORD_TYPE_HAS_PIN(hdr->type) &&
			ACCESS_RECORD_TYPE_CARD(hdr->type) == ACCESS_RECORD_TYPE_CARD_ID;

	case ACCESS_TYPE_CARD_AND_PIN:
		return ACCESS_RECORD_TYPE_PIN(hdr->type) == ACCESS_RECORD_TYPE_PIN_FIXED &&
			ACCESS_RECORD_TYPE_CARD(hdr->type) == ACCESS_RECORD_TYPE_CARD_ID;

	default:
		return 0;
	}
}

static int8_t access_record_match(
	uint8_t access_type, uint32_t key, const struct access_record_v2 *rec)
{
	switch(access_type) {
	case ACCESS_TYPE_PIN:
		return rec->pin.fixed == key;

	case ACCESS_TYPE_CARD:
		return rec->card == key;

	case ACCESS_TYPE_CARD_AND_PIN:
		return (rec->card ^ rec->pin.fixed) == key;

	default:
		return 0;
	}
}

int8_t eeprom_has_access(uint8_t type, uint32_t key, uint8_t door_id)
{
	struct access_record_match match = {
		.type = type,
		.doors = BIT(door_id),
	};
	struct access_record_v2 rec;
	uint16_t idx;

	eeprom_for_each_access_record_where(
		idx, &rec, access_record_filter, &match) {

		if (access_record_match(type, key, &rec)) {
			if (!rec.hdr.used) {
				rec.hdr.used = 1;
				eeprom_write_access_record(idx, &rec);
			}
			return 0;
		}
	}

	return -EPERM;
}
