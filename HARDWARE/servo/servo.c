#include "servo.h"

// 舵机初始化
void servo_init(void)
{
    // 1. 使能时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM4, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);

    // 2. 配置GPIO为复用功能
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_14;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOD, &GPIO_InitStructure);

    // 3. 连接GPIO到TIM4通道3
    GPIO_PinAFConfig(GPIOD, GPIO_PinSource14, GPIO_AF_TIM4);

    // 4. 配置定时器 - 产生20ms周期的PWM
    TIM_TimeBaseStructure.TIM_Prescaler = 84 - 1;      // 1MHz计数频率
    TIM_TimeBaseStructure.TIM_Period = 20000 - 1;      // 20ms周期
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM4, &TIM_TimeBaseStructure);

    // 5. 配置PWM输出通道
    TIM_OCInitStructure.TIM_OCMode = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse = 500;                // 初始0.5ms(0度，关锁状态)
    TIM_OCInitStructure.TIM_OCPolarity = TIM_OCPolarity_High;
    TIM_OC3Init(TIM4, &TIM_OCInitStructure);

    // 6. 使能预装载和定时器
    TIM_OC3PreloadConfig(TIM4, TIM_OCPreload_Enable);
    TIM_ARRPreloadConfig(TIM4, ENABLE);
    TIM_Cmd(TIM4, ENABLE);
}

// 设置舵机角度
// 0度: 500 (0.5ms)
// 90度: 1500 (1.5ms)
void servo_angle(uint8_t angle)
{
    if (angle == 0)
    {
        TIM_SetCompare3(TIM4, 500);    // 0度 - 关锁
    }
    else if (angle == 90)
    {
        TIM_SetCompare3(TIM4, 1500);   // 90度 - 开锁
    }
}
