/**
  ******************************************************************************
  * @文件     remote_cmd.h
  * @摘要     远程命令层 —— WiFi(MQTT) 与蓝牙(BLE) 两条链路共用
  *
  *   收到的命令一律是 ASCII 行文本，处理动作与"按按键"完全等价：
  *     命令通过事件标志组投递给 task_as608 / task_rfid / task_keyboard，
  *     开锁指令通过 g_servo_queue 投递给舵机任务。
  *   这样远程控制不需要在业务任务里加任何分支。
  *
  *   ---- 小程序 -> 设备 ----
  *     unlock              开锁
  *     close               上锁
  *     state               查询门锁状态
  *     fp_add              进入录入指纹流程
  *     fp_del              清空指纹库
  *     fp_num              查询已录指纹数量
  *     card_add            进入添加 RFID 卡流程
  *     card_del            清空已登记的卡
  *     card_num            查询已登记卡数量
  *     pwd:123456          用密码开锁
  *     pwd_set:654321      修改开锁密码
  *     ping                链路探测
  *     help                命令列表
  *
  *   ---- 设备 -> 小程序 ----
  *     state=lock | state=unlock
  *     ack=<命令名> | ack=pwd_ok | ack=pwd_fail | ack=pwd_set_ok | ack=pwd_set_bad
  *     num=fp,<n> | num=card,<n>
  *     pong | err=unknown | help=<命令列表>
  ******************************************************************************
**/

#ifndef __REMOTE_CMD_H
#define __REMOTE_CMD_H

#include "include.h"

/** 单行命令最大长度 */
#define REMOTE_LINE_MAX 64

/** 通道刚上线时调用：把当前状态与数量主动上报一次 */
void remote_cmd_on_online(void);

/**
 * @brief  处理一条收到的命令（原地修改传入缓冲，去掉首尾空白）
 */
void remote_cmd_handle_line(char *line);

/** 门锁状态变化时上报（由舵机任务调用） */
void remote_cmd_report_state(void);

/** 向当前通道发送一条文本（自动补行尾） */
void remote_cmd_send(const char *msg);

#endif /* __REMOTE_CMD_H */
