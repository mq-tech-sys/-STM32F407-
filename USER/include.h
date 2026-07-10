#ifndef __INCLUDE_H
#define __INCLUDE_H

#include <stm32f4xx.h>
#include <stdio.h>
#include <string.h>
#include "sys.h"
#include "delay.h"
#include "esp8266.h"
#include "usart.h"
#include "servo.h"
#include "keyboard.h"
#include "oled.h"
#include "myimagedot.h"
#include "as608_uart2.h"
#include "as608.h"
#include "beep.h"
#include "key.h"
#include "tim.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"
#include "ble.h"
#include "stdarg.h"
#include "MFRC522.h"

/* 按键事件标志位 */
#define EVENT_KEY1_SHORT (1 << 0)
#define EVENT_KEY2_SHORT (1 << 1)
#define EVENT_KEY3_SHORT (1 << 2)

#define EVENT_KEY1_LONG (1 << 3)
#define EVENT_KEY2_LONG (1 << 4)
#define EVENT_KEY3_LONG (1 << 5)

#define EVENT_KEY1_DOUBLE (1 << 6)
#define EVENT_KEY2_DOUBLE (1 << 7)
#define EVENT_KEY3_DOUBLE (1 << 8)
/*蓝牙，wifi2选1*/
#define USART3_WIFI_USE 1
#define USART3_BLE_USE 0
/*蓝牙,wifi波特率*/
#define WIFI_BAUD 115200
#define BLE_BAUD 9600
// 全局变量
extern GPIO_InitTypeDef GPIO_InitStructure;
extern NVIC_InitTypeDef NVIC_InitStructure;
extern USART_InitTypeDef USART_InitStructure;
extern EXTI_InitTypeDef EXTI_InitStructure;
extern TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
extern TIM_OCInitTypeDef TIM_OCInitStructure;

extern QueueHandle_t g_servo_queue;        // 舵机消息队列
extern uint8_t g_lock_state;               //  锁标志
extern EventGroupHandle_t g_event_group;   // 事件标志组
extern SemaphoreHandle_t g_mutex_function; // 互斥锁
extern SemaphoreHandle_t g_mutex_printf;
extern SemaphoreHandle_t g_mutex_rfid;

//extern u8  mfrc552pidbuf[18];
//extern u8  card_pydebuf[2];
//extern u8  card_numberbuf[5];
//extern u8  card_key0Abuf[6];
//extern u8  card_writebuf[16];
//extern u8  card_readbuf[18];

typedef  struct
{   
    uint8_t uid[4];
}CARD_UID;

extern  CARD_UID g_card_list[10];
extern  uint8_t g_card_count ;

extern void dgb_printf_safe(const char *format, ...);
#endif
