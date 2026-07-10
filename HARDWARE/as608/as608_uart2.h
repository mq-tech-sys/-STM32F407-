/**
  ******************************************************************************
  * @文件名： beep.h
  * @作者：   GEC_方惠清
  * @版本：	  V1.0.0
  * @日期：   2023.6.2
  * @摘要：   指纹模块的串口2
  *          
  ******************************************************************************
**/

#ifndef __AS608_UART2_H	//定义以防止递归包含
#define __AS608_UART2_H


//一、函数的头文件
#include "stm32f4xx.h"
#include "delay.h"
#include "sys.h"


//二、宏定义(命令、数据、函数)、数据类型(枚举、结构体)
#define usart2_baund  57600							//串口2波特率，根据指纹模块波特率更改
#define USART2_MAX_RECV_LEN		800					//最大接收缓存字节数
#define USART2_MAX_SEND_LEN		800					//最大发送缓存字节数
#define USART2_RX_EN 			1					//0,不接收;1,接收.

extern u8  USART2_RX_BUF[USART2_MAX_RECV_LEN]; 		//接收缓冲,最大USART2_MAX_RECV_LEN字节
extern u8  USART2_TX_BUF[USART2_MAX_SEND_LEN]; 		//发送缓冲,最大USART2_MAX_SEND_LEN字节
extern vu16 USART2_RX_STA;   						//接收数据状态


//三、全局变量、函数声明
//1、串口2接收中断
extern void USART2_IRQHandler(void);
//2、串口2初始化
extern void usart2_init(u32 pclk1,u32 bound);
//3、串口2发送数据
extern void u2_printf(char* fmt,...);





#endif /*__AS608_UART2_H */
