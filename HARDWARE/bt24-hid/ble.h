#ifndef __BLE_H
#define __BLE_H

#include "include.h"

extern uint8_t g_ble_rx_buf[256];
extern uint32_t g_ble_cnt;

extern void ble24_init(void);
extern void ble_process_loop(void);
extern void ble_send_lock_state(uint8_t state);

#endif
