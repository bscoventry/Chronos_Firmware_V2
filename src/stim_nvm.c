/*
 * Stimulation NVM record: magic, version, flags, stim_setting, CRC32 (IEEE).
 * Stored under Settings key "chronos/stim".
 */
#if defined(CONFIG_BT) && !defined(CONFIG_SPI) && defined(CONFIG_CHRONOS_STIM_NVM)

#include <string.h>
#include <errno.h>
#include <stddef.h>

#include <zephyr/settings/settings.h>
#include <zephyr/logging/log.h>

#include "stim_nvm.h"
#include "config.h"

LOG_MODULE_REGISTER(stim_nvm, LOG_LEVEL_INF);

#define SETTINGS_KEY "chronos/stim"

struct stim_nvm_record {
	uint32_t magic;
	uint16_t version;
	uint16_t flags;
	stim_setting stim;
	uint32_t crc32;
} __attribute__((packed));

static struct stim_nvm_record s_store;
static bool s_have_record;
static bool s_ble_gate;
static bool s_wants_boot_resume;

static uint32_t crc32_ieee(const uint8_t *data, size_t len)
{
	uint32_t crc = 0xFFFFFFFFu;

	for (size_t i = 0; i < len; i++) {
		crc ^= data[i];
		for (int j = 0; j < 8; j++) {
			if ((crc & 1u) != 0u) {
				crc = (crc >> 1) ^ 0xEDB88320u;
			} else {
				crc >>= 1;
			}
		}
	}
	return ~crc;
}

static uint32_t record_crc(const struct stim_nvm_record *r)
{
	return crc32_ieee((const uint8_t *)r, offsetof(struct stim_nvm_record, crc32));
}

static bool stim_params_valid(const stim_setting *s)
{
	if (s->frequency < 1u || s->frequency > 500u) {
		return false;
	}
	if (s->pulse_width < 10u || s->pulse_width > 5000u) {
		return false;
	}
	if (s->DAC_amplitude == 0u) {
		return false;
	}
	return true;
}

static int chronos_settings_set(const char *name, size_t len,
				settings_read_cb read_cb, void *cb_arg)
{
	if (strcmp(name, "stim") != 0) {
		return -ENOENT;
	}

	if (len != sizeof(struct stim_nvm_record)) {
		LOG_WRN("stim NVM: bad len %zu", len);
		return -EINVAL;
	}

	int rc = read_cb(cb_arg, &s_store, sizeof(s_store));

	if (rc < 0) {
		return rc;
	}

	s_have_record = true;
	return 0;
}

static int chronos_settings_get(const char *name, char *val, int val_len_max)
{
	if (strcmp(name, "stim") != 0) {
		return -ENOENT;
	}

	if (!s_have_record) {
		return -ENOENT;
	}

	if (val_len_max < (int)sizeof(s_store)) {
		return -ENOMEM;
	}

	memcpy(val, &s_store, sizeof(s_store));
	return sizeof(s_store);
}

static int chronos_settings_export(int (*storage_func)(const char *name,
			       const void *value, size_t val_len))
{
	if (!s_have_record || storage_func == NULL) {
		return 0;
	}
	return storage_func("stim", &s_store, sizeof(s_store));
}

static int chronos_settings_commit(void)
{
	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(chronos_stim, "chronos", chronos_settings_get,
			       chronos_settings_set, chronos_settings_commit,
			       chronos_settings_export);

static int validate_and_apply(stim_setting *settings_out, stim_setting *pending_out)
{
	if (!s_have_record) {
		LOG_INF("stim NVM: no record");
		return -EIO;
	}

	if (s_store.magic != STIM_NVM_MAGIC || s_store.version != STIM_NVM_VERSION) {
		LOG_WRN("stim NVM: bad magic/version");
		return -EIO;
	}

	if (s_store.crc32 != record_crc(&s_store)) {
		LOG_WRN("stim NVM: CRC mismatch");
		return -EIO;
	}

	if (!stim_params_valid(&s_store.stim)) {
		LOG_WRN("stim NVM: params out of range");
		return -EIO;
	}

	memcpy(settings_out, &s_store.stim, sizeof(stim_setting));
	memcpy(pending_out, &s_store.stim, sizeof(stim_setting));

	s_ble_gate = false;
	s_wants_boot_resume = ((s_store.flags & STIM_NVM_FLAG_ENABLED) != 0u);

	LOG_INF("stim NVM: OK enabled=%d", s_wants_boot_resume ? 1 : 0);
	return 0;
}

int stim_nvm_boot_load(stim_setting *settings_out, stim_setting *pending_out)
{
	int err;

	s_have_record = false;
	s_ble_gate = true;
	s_wants_boot_resume = false;

	err = settings_subsys_init();
	if (err != 0) {
		LOG_ERR("settings_subsys_init %d", err);
		return err;
	}

	err = settings_load();
	if (err != 0) {
		LOG_WRN("settings_load %d", err);
	}

	err = validate_and_apply(settings_out, pending_out);
	if (err != 0) {
		s_ble_gate = true;
		s_wants_boot_resume = false;
		return err;
	}

	return 0;
}

bool stim_nvm_ble_stim_blocked(void)
{
	return s_ble_gate;
}

bool stim_nvm_wants_boot_resume(void)
{
	return s_wants_boot_resume;
}

int stim_nvm_save(bool stim_enabled, const stim_setting *s)
{
	if (!stim_params_valid(s)) {
		return -EINVAL;
	}

	struct stim_nvm_record rec;

	memset(&rec, 0, sizeof(rec));
	rec.magic = STIM_NVM_MAGIC;
	rec.version = STIM_NVM_VERSION;
	rec.flags = stim_enabled ? STIM_NVM_FLAG_ENABLED : 0u;
	memcpy(&rec.stim, s, sizeof(stim_setting));
	rec.crc32 = record_crc(&rec);

	int 	err = settings_save_one(SETTINGS_KEY, &rec, sizeof(rec));

	if (err != 0) {
		LOG_ERR("settings_save_one %d", err);
		return err;
	}

	memcpy(&s_store, &rec, sizeof(rec));
	s_have_record = true;
	s_ble_gate = false;

	return 0;
}

#else /* !CONFIG_BT || CONFIG_SPI || !CONFIG_CHRONOS_STIM_NVM */

#include <zephyr/kernel.h>
#include "stim_nvm.h"

int stim_nvm_boot_load(stim_setting *settings_out, stim_setting *pending_out)
{
	ARG_UNUSED(settings_out);
	ARG_UNUSED(pending_out);
	return -ENOTSUP;
}

bool stim_nvm_ble_stim_blocked(void)
{
	return false;
}

bool stim_nvm_wants_boot_resume(void)
{
	return false;
}

int stim_nvm_save(bool stim_enabled, const stim_setting *s)
{
	ARG_UNUSED(stim_enabled);
	ARG_UNUSED(s);
	return -ENOTSUP;
}

#endif /* CONFIG_BT && !CONFIG_SPI && CONFIG_CHRONOS_STIM_NVM */
