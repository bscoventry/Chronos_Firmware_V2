#ifndef BLE_SESSION_H
#define BLE_SESSION_H

#if defined(CONFIG_BT)

/** Magnetic reed on P1.04: debounced edges toggle BLE session on/off. */
void ble_session_init(void);

/** Reset 5-minute inactivity timeout (call on each NUS RX). */
void ble_session_feed_activity(void);

#else

static inline void ble_session_init(void) {}
static inline void ble_session_feed_activity(void) {}

#endif

#endif /* BLE_SESSION_H */
