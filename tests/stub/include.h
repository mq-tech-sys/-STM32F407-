/**
  ******************************************************************************
  * @文件     stub/include.h
  * @摘要     仅供 tests/check_syntax.sh 做"语法自检"用的假 include.h
  *
  *   真实的 USER/include.h 会把 stm32f4xx.h / FreeRTOS / 各外设驱动全拖进来，
  *   本机没有 Keil 与 ARM 头文件，所以这里用最小声明把新写的网络层
  *   （bemfa_mqtt.c / remote_cmd.c / ble.c）单独编译一遍，
  *   目的是抓出拼写错误、函数原型不匹配、漏分号这一类问题。
  *
  *   ⚠️ 它只做语法与原型检查，不能替代在 Keil 里真正编译一次。
  ******************************************************************************
**/

#ifndef __STUB_INCLUDE_H
#define __STUB_INCLUDE_H

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ---- 工程里的简写 ---- */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* ---- FreeRTOS ---- */
typedef uint32_t TickType_t;
typedef uint32_t EventBits_t;
typedef int      UBaseType_t;
typedef void    *QueueHandle_t;
typedef void    *SemaphoreHandle_t;
typedef void    *EventGroupHandle_t;
typedef void    *TaskHandle_t;

#define pdTRUE         1
#define pdFALSE        0
#define pdPASS         1
#define portMAX_DELAY  0xFFFFFFFFUL
#define pdMS_TO_TICKS(x) ((TickType_t)(x))

TickType_t  xTaskGetTickCount(void);
void        vTaskDelay(TickType_t t);
QueueHandle_t xQueueCreate(UBaseType_t len, UBaseType_t size);
int         xQueueSend(QueueHandle_t q, const void *item, TickType_t wait);
int         xQueueReceive(QueueHandle_t q, void *item, TickType_t wait);
EventBits_t xEventGroupSetBits(EventGroupHandle_t g, EventBits_t bits);
EventBits_t xEventGroupWaitBits(EventGroupHandle_t g, EventBits_t bits,
                                int clear, int wait_all, TickType_t wait);

/* ---- 串口（真实实现只在 SYSTEM/usart） ---- */
typedef struct { uint32_t SR; uint32_t DR; } USART_TypeDef;
#define USART3 ((USART_TypeDef *)0x40004800UL)
#define USART1 ((USART_TypeDef *)0x40011000UL)

void     usart3_init(void);
void     usart_send_string(USART_TypeDef *u, char *s);
void     usart_send_buf(USART_TypeDef *u, const uint8_t *buf, uint16_t len);
uint16_t uart3_read(uint8_t *dst, uint16_t max);
void     uart3_rx_flush(void);

/* ---- USART3 通道二选一开关 ---- */
#define USART3_WIFI_USE 1
#define USART3_BLE_USE  0
#define WIFI_BAUD       115200
#define BLE_BAUD        9600

/* ---- 全局量 ---- */
extern EventGroupHandle_t g_event_group;
extern QueueHandle_t      g_servo_queue;
extern uint8_t            g_lock_state;
extern uint8_t            g_card_count;
extern char               g_password[];

#define PASSWORD_MAX_LEN 6

/* ---- 事件标志位 ---- */
#define EVENT_KEY1_SHORT  (1 << 0)
#define EVENT_KEY2_SHORT  (1 << 1)
#define EVENT_KEY3_SHORT  (1 << 2)
#define EVENT_KEY1_LONG   (1 << 3)
#define EVENT_KEY2_LONG   (1 << 4)
#define EVENT_KEY3_LONG   (1 << 5)
#define EVENT_KEY1_DOUBLE (1 << 6)
#define EVENT_KEY2_DOUBLE (1 << 7)
#define EVENT_KEY3_DOUBLE (1 << 8)

/* ---- AS608 ---- */
u8 PS_ValidTempleteNum(u16 *ValidN);

/* ---- 网络层（真实定义在各自的头文件里） ---- */
void  remote_cmd_on_online(void);
void  remote_cmd_handle_line(char *line);
void  remote_cmd_report_state(void);
void  remote_cmd_send(const char *msg);
uint8_t bemfa_mqtt_publish(const char *msg);
uint8_t bemfa_mqtt_online(void);
void  bemfa_mqtt_init(void);
void  bemfa_mqtt_task(void);
void  ble24_init(void);
void  ble24_task(void);

#endif /* __STUB_INCLUDE_H */
