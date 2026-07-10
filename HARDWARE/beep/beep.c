#include "beep.h"


//1、beep初始化
void Beep_Init(void)
{
	//0、GPIO信息配置结构体
	GPIO_InitTypeDef  GPIO_InitStructure;

	//1、使能GPIOF的硬件时钟
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOF, ENABLE);

	//2、配置GPIO8引脚为输出推挽模式
	GPIO_InitStructure.GPIO_Pin 	= GPIO_Pin_8;			//引脚：第8根引脚
	GPIO_InitStructure.GPIO_Mode 	= GPIO_Mode_OUT;		//模式：输出模式
	GPIO_InitStructure.GPIO_OType 	= GPIO_OType_PP;		//输出类型：推挽功能
	GPIO_InitStructure.GPIO_Speed 	= GPIO_Speed_100MHz;	//输出速度：100MHZ，高速
	GPIO_InitStructure.GPIO_PuPd 	= GPIO_PuPd_NOPULL;		//上下拉：	不拉(根据芯片外的外围电路决定拉还是不拉)
	GPIO_Init(GPIOF, &GPIO_InitStructure);					//将配置好的GPIO信息，通过此函数写到相应的寄存器中

	//3、设置引脚的高低电平值
	//GPIO_SetBits(GPIOF, GPIO_Pin_8);						//设置高电平，让蜂鸣器响
	GPIO_ResetBits(GPIOF, GPIO_Pin_8); 						//设置低电平，让蜂鸣器不响
}



//2、beep提示音
void Beep_Hint(void)
{
	BEEP(1);				
	delay_ms(100);	    				
	BEEP(0);	 
}

//3、beep警告音
void Beep_Warning(void)
{
	uint8_t i = 0;
	for(i=0; i<10; i++)		
	{	                            
		BEEP(1);			
		delay_ms(200);
		
		BEEP(0);
		delay_ms(200);
	} 
}










