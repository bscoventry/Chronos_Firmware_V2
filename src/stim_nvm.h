#ifndef STIM_NVM_H
#define STIM_NVM_H

#include <stdbool.h>
#include <stdint.h>

#include "data.h"

/** Enable in prj.conf: CONFIG_CHRONOS_STIM_NVM=y plus Settings/NVS (see Kconfig help). */

/** Magic for persisted stim record ('STIM' LE). */
#define STIM_NVM_MAGIC 0x4D495453u

#define STIM_NVM_VERSION 1u

/** Bit 0: stimulation enabled (LV_STIM running when BT build resumes). */
#define STIM_NVM_FLAG_ENABLED 0x0001u

/**
 * Load NVM after settings_subsys_init + settings_register.
 * Validates CRC32 + parameter bounds. Sets BLE gate if corrupt/missing.
 * On success, copies payload into @p settings and @p pending_settings when valid.
 *
 * @return 0 on valid record, -EIO on corrupt/missing/invalid params.
 */
int stim_nvm_boot_load(stim_setting *settings_out, stim_setting *pending_out);

/** True until a valid NVM record exists (CRC OK); false after successful boot load or BLE save. */
bool stim_nvm_ble_stim_blocked(void);

/** True if boot loaded valid NVM with STIM_NVM_FLAG_ENABLED (caller starts stimulation). */
bool stim_nvm_wants_boot_resume(void);

/**
 * Persist stim state + parameters. Updates CRC32. Clears BLE gate on success.
 * Call after successful BLE START (enabled=true) or STOP (enabled=false).
 */
int stim_nvm_save(bool stim_enabled, const stim_setting *s);

#endif /* STIM_NVM_H */
