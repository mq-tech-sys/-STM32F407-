#include "tim.h"


void tim2_init(void)
{
	//使能TIM2的硬件时钟，就是对它供电
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2,ENABLE);
	
	//配置TIM2相关的参数：预分频值、自动重载值（决定了定时时间）、计数方向（递增/递减）、使能触发中断
	//TIM2的硬件时钟频率=APB1 x 2/8400=84000000Hz/8400=10000Hz
	TIM_TimeBaseStructure.TIM_Period = 10000/100-1;
	TIM_TimeBaseStructure.TIM_Prescaler = 8400-1; //预分频值，该值传递给寄存器时自动加1
	TIM_TimeBaseStructure.TIM_ClockDivision = 0;  //该定时器是没有时钟分频，怎么设置都不会生效
	TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;//递增计数，从0开始计数，计数到TIM_Period就会触发中断
	TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);
	
	//使能时间更新中断
	TIM_ITConfig(TIM2,TIM_IT_Update , ENABLE);
	
	//清空标志位，否则会一开始运行触发中断
	TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
	//配置TIM2的NVIC
	NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);	
	
	//使能TIM2工作
	TIM_Cmd(TIM2, ENABLE);

}

