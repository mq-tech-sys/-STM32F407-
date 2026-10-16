#ifndef __BLE_H
#define __BLE_H

#include "include.h"

/** 单行命令最大长度，与 remote_cmd 的约定保持一致 */
#define BLE_LINE_MAX 64

/** 手机端可能不发换行符，超过这个毫秒数没收到新字节就把已收内容当成一整条命令 */
#define BLE_IDLE_FLUSH_MS 100

/** 初始化（串口3，波特率由 include.h 的 BLE_BAUD 决定） */
void ble24_init(void);

/** 10ms 调一次：收字节 -> 按行切分 -> 交给 remote_cmd 处理 */
void ble24_task(void);

#endif
