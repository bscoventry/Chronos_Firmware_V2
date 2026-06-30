#ifndef BLE_SESSION_H
#define BLE_SESSION_H

#if defined(CONFIG_BT)

/** Reed P1.15 (NO, swipe to VCC): each rising edge toggles BLE after lockout (ble_session.c). */
void ble_session_init(void);

/** Reset 5-minute inactivity timeout (call on each NUS RX). */
void ble_session_feed_activity(void);

#else

static inline void ble_session_init(void) {}
static inline void ble_session_feed_activity(void) {}

#endif

#endif /* BLE_SESSION_H */
