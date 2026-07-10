#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "stm32f4xx_conf.h"
#include "sys.h"
#include "include.h"

// 串口3事件标志，供wifi和蓝牙共用
extern uint32_t g_usart_event;

//如果想串口中断接收，请不要注释以下宏定义
extern void uart_init(u32 baud);
extern void usart_send_string(USART_TypeDef* USARTx, char *str);
extern void usart3_init(void);
#endif


