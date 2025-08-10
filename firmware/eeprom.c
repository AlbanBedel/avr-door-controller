#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <avr/eeprom.h>
#include "eeprom.h"
#include "utils.h"

static struct eeprom_config config EEMEM;

static int8_t eeprom_entry_is_in_bounds(uint16_t idx, uint8_t len)
{
	uint16_t end = idx + len;
	/* Check that we don't overflow */
	return end > idx && end <= ARRAY_SIZE(config.access);
}

static struct access_record_entry *eeprom_entry_at(uint16_t idx, uint8_t len)
{
	return eeprom_entry_is_in_bounds(idx, len) ? &config.access[idx] : NULL;
}

static int8_t eeprom_entry_clear(uint16_t idx)
{
	struct access_record_hdr hdr = {};
	struct access_record_entry *eep;

	eep = eeprom_entry_at(idx, 1);
	if (!eep)
		return -EINVAL;

	eeprom_write_block(&hdr, eep, sizeof(hdr));
	return 0;
}

static int8_t eeprom_read_access_record_hdr(
	uint16_t idx, struct access_record_hdr *hdr)
{
	struct access_record_entry *eep;

	eep = eeprom_entry_at(idx, 1);
	if (!eep)
		return -EINVAL;

	eeprom_read_block(hdr, eep, sizeof(*hdr));
	return 0;
}

static int8_t eeprom_read_access_record_data(
	uint16_t idx, struct access_record_v2 *rec)
{
	struct access_record_entry *eep;

	eep = eeprom_entry_at(idx, ACCESS_RECORD_ENTRIES(rec));
	if (!eep)
		return -EINVAL;

	if (ACCESS_RECORD_HAS_CARD(rec)) {
		eeprom_read_block(&rec->card, &eep->card, sizeof(rec->card));
		eep++;
	} else {
		rec->card = 0;
	}

	if (ACCESS_RECORD_HAS_PIN(rec))
		eeprom_read_block(&rec->pin, &eep->pin, sizeof(rec->pin));
	else
		rec->pin.fixed = 0;

	return 0;
}

int8_t eeprom_read_access_record(uint16_t idx, struct access_record_v2 *rec)
{
	int8_t err;

	err = eeprom_read_access_record_hdr(idx, &rec->hdr);
	if (err)
		return err;

	if (ACCESS_RECORD_IS_EMPTY(rec))
		return -ENOENT;

	if (ACCESS_RECORD_IS_CONTINUATION(rec))
		return -EBUSY;

	return eeprom_read_access_record_data(idx, rec);
}

int8_t eeprom_update_access_record_hdr(
	uint16_t idx, const struct access_record_hdr *hdr)
{
	struct access_record_hdr old_hdr;
	struct access_record_entry *eep;

	/* Don't allow editing empty records or turning them in a continuation */
	if (hdr->type == ACCESS_RECORD_TYPE(NONE, NONE) || hdr->doors == 0)
		return -EINVAL;

	/* Validate the index */
	eep = eeprom_entry_at(idx, 1);
	if (!eep)
		return -EINVAL;

	/* Check that we don't update the type of the record */
	eeprom_read_block(&old_hdr, eep, sizeof(old_hdr));
	if (hdr->type != old_hdr.type || old_hdr.doors == 0)
		return -EINVAL;

	if (memcmp(&old_hdr, hdr, sizeof(*hdr)))
		eeprom_write_block(hdr, eep, sizeof(*hdr));

	return 0;
}

int8_t eeprom_write_access_record(
	uint16_t idx, const struct access_record_v2 *rec)
{
	uint8_t n = 0, i, old_len, new_len = ACCESS_RECORD_ENTRIES(rec);
	struct access_record_entry entry;
	struct access_record_entry *eep;
	int8_t err;

	eep = eeprom_entry_at(idx, ACCESS_RECORD_ENTRIES(rec));
	if (!eep)
		return -EINVAL;

	/* Make sure we don't write in the middle of a record */
	err = eeprom_read_access_record_hdr(idx, &entry.hdr);
	if (err)
		return err;
	if (ACCESS_RECORD_IS_CONTINUATION(&entry))
		return -EBUSY;

	old_len = ACCESS_RECORD_ENTRIES(&entry);

	/* Check that we won't overwrite a following record */
	for (i = 1; i < new_len && idx + i > idx; i++) {
		eeprom_read_access_record_hdr(idx + i, &entry.hdr);
		if (!(ACCESS_RECORD_IS_EMPTY(&entry) ||
		      ACCESS_RECORD_IS_CONTINUATION(&entry)))
			return -EBUSY;
	}

	/* Write the new entries */
	if (!ACCESS_RECORD_IS_EMPTY(rec) && rec->hdr.doors) {
		entry.hdr = rec->hdr;

		if (ACCESS_RECORD_HAS_CARD(rec)) {
			entry.card = rec->card;
			eeprom_write_block(&entry, eep + n, sizeof(entry));

			/* Clear the used flag, doors mask and card type
			 * for the continuation entries */
			entry.hdr.used = 0;
			entry.hdr.doors = 0;
			entry.hdr.type &= ~ACCESS_RECORD_TYPE_CARD(-1);

			/* Advance the write pointer */
			n++;
		}

		if (ACCESS_RECORD_HAS_PIN(rec)) {
			entry.pin = rec->pin;
			eeprom_write_block(&entry, eep + n, sizeof(entry));

			/* Advance the write pointer */
			n++;
		}
	}

	/* Clear the left over entries, but stop if we ever hit an entry
	 * which is not a continuation. If n is 0 we are erasing the record,
	 * so clear the first entry unconditionally. */
	for ( ; n < old_len; n++) {
		if (n > 0)
			eeprom_read_access_record_hdr(idx + n, &entry.hdr);
		if (n == 0 || ACCESS_RECORD_IS_CONTINUATION(&entry))
			eeprom_entry_clear(idx + n);
		else
			break;
	}

	return 0;
}

int8_t
eeprom_get_next_access_record(
	uint16_t *idx, struct access_record_v2 *rec,
	eeprom_check_access_record_t check, const void *check_ctx)
{
	int8_t err;

	if (*idx == ACCESS_RECORD_ITER_START) {
		*idx = 0;
	} else {
		err = eeprom_read_access_record_hdr(*idx, &rec->hdr);
		if (err)
			return err;
		*idx += ACCESS_RECORD_ENTRIES(rec);
	}

	while (eeprom_entry_is_in_bounds(*idx, 1)) {
		/* Read the header and ignore empty entries */
		err = eeprom_read_access_record_hdr(*idx, &rec->hdr);
		if (err && err != -ENOENT)
			return err;

		if (!err && (!check || check(&rec->hdr, check_ctx) > 0)) {
			err = eeprom_read_access_record_data(*idx, rec);
			return err;
		}

		/* Otherwise go the next one */
		*idx += ACCESS_RECORD_ENTRIES(rec);
	}

	return -ENOENT;
}

uint16_t eeprom_get_free_access_record_count(void)
{
	struct access_record_hdr hdr;
	uint16_t i, count = 0;

	for (i = 0; eeprom_entry_is_in_bounds(i, 1);
	     i += ACCESS_RECORD_TYPE_ENTRIES(hdr.type)) {
		/* Read the header */
		eeprom_read_access_record_hdr(i, &hdr);
		/* Count if it is empty */
		if (hdr.type == ACCESS_RECORD_TYPE(NONE, NONE))
			count++;
	}

	return count;
}

static uint32_t access_record_get_pin(const struct access_record_v2 *rec)
{
	switch (ACCESS_RECORD_PIN_TYPE(rec)) {
	case ACCESS_RECORD_TYPE_PIN_FIXED:
		return rec->pin.fixed;
	case ACCESS_RECORD_TYPE_PIN_HOTP:
	case ACCESS_RECORD_TYPE_PIN_TOTP:
		return rec->pin.otp.key_id;
	default:
		return (uint32_t)-1;
	}
}

static int8_t access_record_header_has_type(
	const struct access_record_hdr *hdr, const void *val)
{
	uint8_t type = *((uint8_t *)val);

	return hdr->type == type;
}

int8_t eeprom_load_access_record(
	uint8_t type, uint32_t card, uint32_t pin, struct access_record_v2 *rec, uint16_t *pos)
{
	uint16_t idx;

	if (type == ACCESS_RECORD_TYPE(NONE, NONE))
		return -EINVAL;

	eeprom_for_each_access_record_where(
			idx, rec, access_record_header_has_type, &type) {
		if (ACCESS_RECORD_TYPE_HAS_CARD(type) && rec->card != card)
			continue;
		if (ACCESS_RECORD_TYPE_HAS_PIN(type) &&
		    access_record_get_pin(rec) != pin)
			continue;
		if (pos)
			*pos = idx;
		return 0;
	}

	return -ENOENT;
}

int8_t eeprom_load_this_access_record(struct access_record_v2 *rec, uint16_t *pos)
{
	return eeprom_load_access_record(
		rec->hdr.type, rec->card, access_record_get_pin(rec), rec, pos);
}

static int8_t eeprom_find_free_entry(uint8_t type, uint16_t *pos)
{
	struct access_record_hdr hdr;
	uint16_t start = -1, idx;

	for (idx = 0; eeprom_entry_is_in_bounds(idx, 1);
	     idx += ACCESS_RECORD_TYPE_ENTRIES(hdr.type)) {
		/* Read the header */
		eeprom_read_access_record_hdr(idx, &hdr);
		/* Restart the search if the entry is not empty */
		if (hdr.type != ACCESS_RECORD_TYPE(NONE, NONE)) {
			start = -1;
			continue;
		}
		/* Save the start of the range */
		if (start == -1)
			start = idx;
		/* Done if we have enough entries */
		if (idx + 1 - start == ACCESS_RECORD_TYPE_ENTRIES(type)) {
			if (pos)
				*pos = start;
			return 0;
		}
	}

	return -ENOSPC;
}

int8_t eeprom_save_access_record(const struct access_record_v2 *rec)
{
	struct access_record_v2 tmp;
	uint16_t idx;
	int8_t err;

	/* Nothing to do to write an empty record */
	if (ACCESS_RECORD_IS_EMPTY(rec))
		return 0;

	/* Lookup where this record is */
	err = eeprom_load_access_record(
		rec->hdr.type, rec->card, access_record_get_pin(rec),
		&tmp, &idx);
	/* Allocate a new one if not found */
	if (err == -ENOENT) {
		if (rec->hdr.doors == 0)
			return 0;
		/* Find a free spot */
		err = eeprom_find_free_entry(rec->hdr.type, &idx);
	}
	if (err)
		return err;

	return eeprom_write_access_record(idx, rec);
}

void eeprom_remove_all_access(void)
{
	struct access_record_hdr hdr;
	uint16_t idx;

	for (idx = 0; idx < ARRAY_SIZE(config.access); idx++) {
		eeprom_read_access_record_hdr(idx, &hdr);
		if (hdr.type != ACCESS_RECORD_TYPE(NONE, NONE))
			eeprom_entry_clear(idx);
	}
}

int8_t eeprom_get_controller_config(struct controller_config *cfg)
{
	eeprom_read_block(cfg, &config.ctrl, sizeof(*cfg));
	return 0;
}

int8_t eeprom_set_controller_config(const struct controller_config *cfg)
{
	eeprom_write_block(cfg, &config.ctrl, sizeof(*cfg));
	return 0;
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
