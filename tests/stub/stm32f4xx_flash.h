/**
  ******************************************************************************
  * @文件     stub/stm32f4xx_flash.h
  * @摘要     仅供 PC 端语法自检用的假 Flash 头文件
  *
  *   本机没有 Keil 与 STM32 标准外设库，用最小声明让 param.c 能单独编译一遍，
  *   抓拼写错误、原型不匹配、漏分号。
  *   ⚠ 只做语法检查，不能替代 Keil 里的真实编译。
  ******************************************************************************
**/

#ifndef __STUB_STM32F4XX_FLASH_H
#define __STUB_STM32F4XX_FLASH_H

#include <stdint.h>

typedef enum
{
    FLASH_BUSY = 1,
    FLASH_ERROR_PGS,
    FLASH_ERROR_PGP,
    FLASH_ERROR_PGA,
    FLASH_ERROR_WRP,
    FLASH_ERROR_PROGRAM,
    FLASH_ERROR_OPERATION,
    FLASH_COMPLETE
} FLASH_Status;

#define FLASH_Sector_0      0u
#define FLASH_Sector_1      1u
#define FLASH_Sector_2      2u
#define FLASH_Sector_3      3u
#define FLASH_Sector_4      4u
#define FLASH_Sector_5      5u
#define FLASH_Sector_6      6u
#define FLASH_Sector_7      7u

#define VoltageRange_1      0u
#define VoltageRange_2      1u
#define VoltageRange_3      2u

#define FLASH_FLAG_EOP      0x00000001u
#define FLASH_FLAG_OPERR    0x00000002u
#define FLASH_FLAG_WRPERR   0x00000010u
#define FLASH_FLAG_PGAERR   0x00000020u
#define FLASH_FLAG_PGPERR   0x00000040u
#define FLASH_FLAG_PGSERR   0x00000080u

void         FLASH_Unlock(void);
void         FLASH_Lock(void);
void         FLASH_ClearFlag(uint32_t flags);
FLASH_Status FLASH_EraseSector(uint32_t sector, uint8_t voltage_range);
FLASH_Status FLASH_ProgramWord(uint32_t addr, uint32_t data);

#endif /* __STUB_STM32F4XX_FLASH_H */
