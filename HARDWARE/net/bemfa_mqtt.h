/**
  ******************************************************************************
  * @文件     bemfa_mqtt.h
  * @摘要     巴法云 MQTT 接入（ESP8266 AT 透传 + STM32 侧自实现 MQTT 3.1.1）
  *
  *   链路：STM32 USART3 <--AT--> ESP8266 <--TCP--> bemfa.com:9501 <--MQTT-->
  *
  *   和旧版"巴法云 TCP 长连接"的区别：
  *     旧：AT+CIPSTART "TCP","tcp.bemfa.com",8344  +  自定义行文本 cmd=1&uid=..&topic=..
  *     新：AT+CIPSTART "TCP","bemfa.com",9501       +  标准 MQTT 3.1.1 报文
  *   ESP8266 侧固件一个字节都不用换，MQTT 报文由 mqtt_packet.c 在 STM32 上拼装。
  *
  *   双向语义（MQTT 控制台里只需建一个主题）：
  *     设备订阅  <BAFA_TOPIC_LOCK>          <- 收到小程序发往 <topic>/set 的命令
  *     设备发布  <BAFA_TOPIC_LOCK>/set      -> 推送给订阅了该主题的小程序
  *   （巴法云规定：发布时主题名加 /set 才推送；发布者自己订阅该主题时不会收到自己的消息，
  *     所以设备与小程序共用同一主题不会自激。）
  ******************************************************************************
**/

#ifndef __BEMFA_MQTT_H
#define __BEMFA_MQTT_H

#include "include.h"

/* ---------------------------------------------------------------------------
 * 需要你按自己环境修改的配置
 * --------------------------------------------------------------------------- */

/** 家里/实验室的 WiFi，只能 2.4G，ESP8266 不支持 5G */
#define WIFI_NAME          "ymq"
#define WIFI_PASSWORD      "88888888"

/** 巴法云 MQTT 控制台 -> 你的私钥（32 位十六进制），用作 MQTT ClientID */
#define BAFA_UID           "28eccdb351e4bf84ad1390c143846844"

/** 巴法云 MQTT 控制台 -> 主题名 */
#define BAFA_TOPIC_LOCK    "lock000"

/** MQTT 服务器 */
#define BAFA_HOST          "bemfa.com"
#define BAFA_PORT          9501          /* 明文 MQTT 端口；加密用 9503（需 TLS，本工程未实现） */

/** MQTT KeepAlive（秒）。心跳按它的一半发送，即 30s 一次 PINGREQ */
#define MQTT_KEEPALIVE_S   60

/**
 * 设备连续多少秒没收到服务器任何字节就判定掉线并重连。
 * 正常在线时每 30s 会收到一次 PINGRESP，所以 90s 静默基本可以确定链路已断。
 */
#define MQTT_DEAD_SILENCE_S 90

/* ---------------------------------------------------------------------------
 * 接口
 * --------------------------------------------------------------------------- */

/** 初始化（只做串口初始化，真正联网在任务里推进） */
void bemfa_mqtt_init(void);

/**
 * @brief  MQTT 状态机推进，10ms 调一次
 * @note   全非阻塞：联网、订阅、心跳、断线重连都在这里按 tick 推进，
 *         不会阻塞按键 / 舵机 / OLED 任务
 */
void bemfa_mqtt_task(void);

/**
 * @brief  向 <BAFA_TOPIC_LOCK>/set 发布一条状态/应答
 * @param  msg 以 '\0' 结尾的 ASCII 文本（内部去掉了结尾的 \r\n）
 * @retval 1 发布成功，0 未在线或报文过长
 */
uint8_t bemfa_mqtt_publish(const char *msg);

/** 当前 MQTT 是否已连接且已订阅成功 */
uint8_t bemfa_mqtt_online(void);

#endif /* __BEMFA_MQTT_H */
