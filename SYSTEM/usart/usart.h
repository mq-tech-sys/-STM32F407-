#ifndef __USART_H
#define __USART_H
#include "stdio.h"	
#include "stm32f4xx_conf.h"
#include "sys.h"
#include "include.h"

//如果想串口中断接收，请不要注释以下宏定义
extern void uart_init(u32 baud);
extern void usart_send_string(USART_TypeDef* USARTx, char *str);

/**
 * @brief  按长度发送二进制数据（MQTT 报文中可能含 0x00，不能用字符串发送）
 */
extern void usart_send_buf(USART_TypeDef* USARTx, const uint8_t *buf, uint16_t len);

//串口3初始化（WiFi 或蓝牙模块的透传串口）
extern void usart3_init(void);

/**
 * @brief  从 USART3 接收环形缓冲取数据
 * @param  dst 目标缓冲; max 最多取多少字节
 * @retval 实际取出的字节数（0 表示暂时没数据）
 * @note   中断里写 head、任务里读 tail，天然无锁；
 *         MQTT 是字节流协议，粘包/拆包交给上层 mqtt_parser 处理，
 *         所以这里不再做 IDLE 分帧。
 */
extern uint16_t uart3_read(uint8_t *dst, uint16_t max);

/**
 * @brief  丢弃 USART3 接收缓冲中残留的数据
 * @note   重连前调用，避免把断线前/AT 回显的残字节喂给 MQTT 解析器
 */
extern void uart3_rx_flush(void);

/**
 * @brief  USART3 接收溢出（环形缓冲满）时被丢弃的字节数
 */
extern uint32_t uart3_rx_dropped(void);
#endif


