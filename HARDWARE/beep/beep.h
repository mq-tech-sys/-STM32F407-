/**
  ******************************************************************************
  * @文件名： beep.h
  * @作者：   GEC_方惠清
  * @版本：	  V2.0.0
  * @日期：   2023.5.16
  * @摘要：   控制beep响和不响
  *          
  ******************************************************************************
**/

#ifndef __BEEP_H	//定义以防止递归包含
#define __BEEP_H


//一、函数的头文件
#include "stm32f4xx.h"
#include "delay.h"
#include "sys.h"

//二、宏定义(命令、数据、函数)、数据类型(枚举、结构体)
#define BEEP(x) (x) ? (PFout(8)=1): (PFout(8)=0)


//三、全局变量、函数声明
//1、beep初始化
extern void Beep_Init(void);
//2、beep提示音
extern void Beep_Hint(void);
//3、beep警告音
extern void Beep_Warning(void);
	


#endif /*__BEEP_H */



