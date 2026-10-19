/**
  ******************************************************************************
  * @文件     param.h
  * @摘要     掉电保存（密码 / 卡片列表）
  *
  *   存储介质：STM32F407 内部 Flash 的 S3 扇区，起始 0x0800C000，大小 16KB。
  *   为什么不用 EEPROM：F407 没有片内 EEPROM；板上也没有外挂 24Cxx / W25Qxx。
  *   为什么是 S3：当前固件只占 0x08000000~0x08009704（37.8KB），S3 完全空闲，
  *                而且位置和后续 OTA 分区方案一致（S3 固定留给用户参数，永不搬动）。
  *
  *   ⚠ 前提：Keil 的 IROM1 必须改成 Start 0x08000000 / Size 0xC000（48KB），
  *     否则代码继续长下去会盖掉这个扇区。
  ******************************************************************************
**/

#ifndef __PARAM_H
#define __PARAM_H

#include <stdint.h>

/** 上电时调用：扫描扇区，把密码与卡片列表恢复到 RAM。 */
void param_init(void);

/**
 * @brief  把当前 g_password 写入 Flash
 * @retval 1 成功；0 失败（Flash 错误或扇区写满且压缩失败）
 */
uint8_t param_save_password(void);

/**
 * @brief  把当前 g_card_list / g_card_count 写入 Flash
 * @retval 1 成功；0 失败
 */
uint8_t param_save_cards(void);

/**
 * @brief  恢复出厂：擦掉参数扇区，密码回 DEFAULT_PASSWORD，卡片清空
 * @retval 1 成功；0 失败
 */
uint8_t param_reset_default(void);

#endif /* __PARAM_H */
