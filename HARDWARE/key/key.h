#ifndef __KEY_H 
#define __KEY_H


#include "include.h"

#define KEY1 PAin(0)
#define KEY2 PEin(2)
#define KEY3 PEin(3)


#define KEY_ID_1 1
#define KEY_ID_2 2
#define KEY_ID_3 3

// 按键事件
#define KEY_EVENT_NONE 0
#define KEY_EVENT_SHORT 1
#define KEY_EVENT_LONG 2
#define KEY_EVENT_DOUBLE 3


#define LONG_PRESS_TIME 1000
#define DOUBLE_CLICK_TIME 500

// 函数声明
extern void key_init(void);
extern uint8_t key_scan(uint8_t key_id);

#endif /*__KEY_H */
