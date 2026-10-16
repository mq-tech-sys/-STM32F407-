/**
  ******************************************************************************
  * @文件     mqtt_packet.h
  * @摘要     MQTT 3.1.1 控制报文的编码 / 流式解码（纯逻辑，零硬件依赖）
  *
  *   - 编码：CONNECT / SUBSCRIBE / PUBLISH(QoS0) / PINGREQ / DISCONNECT
  *   - 解码：按字节喂入的流式组包器，自带粘包 / 拆包处理与错误重同步
  *
  *   本文件不包含任何 STM32 头文件，因此可以在 PC 上用 gcc 直接编译做单元自测
  *   （见 tests/test_mqtt.c）。
  ******************************************************************************
**/

#ifndef __MQTT_PACKET_H
#define __MQTT_PACKET_H

#include <stdint.h>
#include <stddef.h>

/* ---- 控制报文类型（固定头高 4 位） ---- */
#define MQTT_TYPE_CONNECT    0x10
#define MQTT_TYPE_CONNACK    0x20
#define MQTT_TYPE_PUBLISH    0x30
#define MQTT_TYPE_PUBACK     0x40
#define MQTT_TYPE_SUBSCRIBE  0x80	/* 编码时需再或上 0x02（协议规定） */
#define MQTT_TYPE_SUBACK     0x90
#define MQTT_TYPE_PINGREQ    0xC0
#define MQTT_TYPE_PINGRESP   0xD0
#define MQTT_TYPE_DISCONNECT 0xE0

/* 单条报文最大字节数（含固定头）。巴法云的命令与状态都很短，256 足够 */
#define MQTT_MAX_PACKET      256

/* ---------------------------------------------------------------------------
 * 编码
 *   全部返回"写入 buf 的字节数"；返回 0 表示参数非法或 cap 不够。
 * --------------------------------------------------------------------------- */
size_t mqtt_encode_connect(uint8_t *buf, size_t cap,
                           const char *client_id,
                           const char *user, const char *pass,
                           uint16_t keepalive_s);

size_t mqtt_encode_subscribe(uint8_t *buf, size_t cap,
                             uint16_t msg_id,
                             const char *topic, uint8_t qos);

size_t mqtt_encode_publish(uint8_t *buf, size_t cap,
                           const char *topic,
                           const uint8_t *payload, size_t plen);

size_t mqtt_encode_pingreq(uint8_t *buf, size_t cap);
size_t mqtt_encode_disconnect(uint8_t *buf, size_t cap);

/* ---------------------------------------------------------------------------
 * 解码
 * --------------------------------------------------------------------------- */
typedef struct {
	uint8_t        type;	/* 报文类型（固定头高 4 位，已含标志位的高 4 位原始值） */
	uint8_t        flags;	/* 固定头低 4 位 */
	const uint8_t *body;	/* 指向所属 parser 的内部缓冲，仅在下一次 feed 之前有效 */
	size_t         blen;	/* body 字节数（不含固定头与剩余长度字段） */
} mqtt_msg_t;

typedef struct {
	uint8_t    buf[MQTT_MAX_PACKET];	/* 组包缓冲：固定头 + 剩余长度 + body */
	uint16_t   len;						/* 已收字节数 */
	uint16_t   total;					/* 当前报文总长度，0 表示还没算出来 */
	uint32_t   remain_len;				/* 剩余长度字段的解析中间值（可变长整数） */
	uint8_t    remain_bytes;			/* 剩余长度字段已读字节数（0~4） */
	uint8_t    state;					/* 0=等固定头 1=读剩余长度 2=读 body */
	mqtt_msg_t msg;						/* 最近一次解析出的完整报文 */
} mqtt_parser_t;

void mqtt_parser_init(mqtt_parser_t *p);

/**
 * @brief  喂入 1 个字节
 * @retval  1 = 凑齐一条完整报文，结果在 p->msg
 *          0 = 还需继续喂
 *         -1 = 报文非法（长度越界 / 剩余长度字段超过 4 字节），已丢弃并复位
 * @note    返回 1 后 p->msg.body 只在"下一次 feed 之前"有效
 */
int mqtt_parser_feed_byte(mqtt_parser_t *p, uint8_t b);

/* ---- 报文取用便捷函数 ---- */

/** CONNACK 是否连接成功（body = [会话标志, 返回码]） */
int mqtt_connack_accepted(const mqtt_msg_t *m);
/** CONNACK 的返回码，非 CONNACK 或长度不足返回 -1 */
int mqtt_connack_code(const mqtt_msg_t *m);

/**
 * @brief  从 PUBLISH 报文里拆出主题和负载（QoS0 / QoS1 都能处理）
 * @retval 0 成功；-1 报文格式非法；-2 topic 缓冲不够
 */
int mqtt_publish_split(const mqtt_msg_t *m,
                       char *topic, size_t topic_cap,
                       const uint8_t **payload, size_t *plen);

#endif /* __MQTT_PACKET_H */
