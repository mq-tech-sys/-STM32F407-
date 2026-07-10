#include "include.h"      // 包含项目所需的所有头文件，包括FreeRTOS、GPIO等
 

/**************************** 按键状态结构体 ****************************/
typedef struct {
    uint8_t state;          // 当前状态：0-空闲，1-按下计时，2-释放处理，3-等待双击
    uint32_t press_time;    // 按键按下的时刻（毫秒）
    uint32_t release_time;  // 按键释放的时刻（毫秒）
    uint8_t click_count;    // 连续单击计数（用于双击检测）
    uint8_t long_pressed;   // 是否已触发长按标志（1表示已触发，0未触发）
} KeyState_t;

/**************************** 按键状态数组（3个按键） ****************************/
static KeyState_t key_state[3] = {0};

/**************************** 读取指定按键的电平（低电平有效） ****************************/
static uint8_t key_read_level(uint8_t key_id)
{
    switch(key_id) {
        case KEY_ID_1: return KEY1;    // 假设 KEY1 定义为 GPIO_ReadInputDataBit(...) 或宏
        case KEY_ID_2: return KEY2;
        case KEY_ID_3: return KEY3;
        default: return 1;             // 无效ID返回高电平（无效）
    }
}

/**************************** 按键GPIO初始化 ****************************/
void key_init(void)
{
    // 使能 GPIOA 时钟（KEY1 连接 PA0）
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
    
    // 使能 GPIOE 时钟（KEY2/KEY3 连接 PE2/PE3/PE4）
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
        
    // 使能 SYSCFG 时钟（用于外部中断，但此模块未使用，为扩展保留）
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

    // 配置 KEY1（PA0）为输入，无上拉/下拉（外部电路已有上拉电阻）
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOA, &GPIO_InitStructure);        

    // 配置 KEY2/KEY3（PE2、PE3、PE4）为输入，无上拉/下拉
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3 | GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
    GPIO_Init(GPIOE, &GPIO_InitStructure);            
}

/**************************** 按键扫描状态机（核心函数） ****************************/
/**
 * @brief  扫描指定按键，检测短按、长按、双击事件
 * @param  key_id   按键ID（KEY_ID_1 ~ KEY_ID_3）
 * @retval 事件类型：KEY_EVENT_NONE / KEY_EVENT_SHORT / KEY_EVENT_LONG / KEY_EVENT_DOUBLE
 * @note   需要在主循环或定时器中断中周期性调用（建议10~20ms间隔）
 */
uint8_t key_scan(uint8_t key_id)
{
    uint8_t event = KEY_EVENT_NONE;      // 返回的事件
    uint8_t key_level;                   // 当前引脚电平（0按下，1释放）
    KeyState_t *ks;                      // 指向当前按键状态结构体
    uint32_t current_time;               // 当前系统时间（毫秒）
    
    // 检查按键ID是否有效
    if(key_id < KEY_ID_1 || key_id > KEY_ID_3) {
        return KEY_EVENT_NONE;
    }
    
    ks = &key_state[key_id - 1];         // 获取对应按键的状态指针
    key_level = key_read_level(key_id);  // 读取实际电平（0=按下，1=释放）
    current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;  // FreeRTOS 系统滴答转毫秒
    
    switch(ks->state) {
        // ========== 状态0：空闲，等待按键按下 ==========
        case 0:
            if(key_level == 0) {                 // 检测到按下（低电平）
                ks->state = 1;                   // 进入按下计时状态
                ks->press_time = current_time;   // 记录按下时刻
                ks->long_pressed = 0;            // 清除长按标志
            }
            break;
            
        // ========== 状态1：按键按下，正在计时 ==========
        case 1:
            if(key_level == 0) {                 // 仍然按住
                // 检查是否达到长按时间阈值
                if(current_time - ks->press_time >= LONG_PRESS_TIME) {
                    ks->long_pressed = 1;        // 标记长按已触发
                }
            } else {                             // 按键已释放（电平变高）
                ks->state = 2;                   // 进入释放处理状态
                ks->release_time = current_time; // 记录释放时刻
            }
            break;
            
        // ========== 状态2：刚刚释放，判断事件类型 ==========
        case 2:
            if(ks->long_pressed) {               // 已触发过长按
                event = KEY_EVENT_LONG;          // 返回长按事件
                ks->state = 0;                   // 回到空闲
                ks->click_count = 0;             // 清空点击计数
            } else {                             // 普通短按（未触发长按）
                ks->click_count++;               // 单击计数+1
                if(ks->click_count >= 2) {       // 连续两次单击（双击）
                    event = KEY_EVENT_DOUBLE;    // 返回双击事件
                    ks->state = 0;               // 回到空闲
                    ks->click_count = 0;
                } else {                         // 第一次单击，等待第二次
                    ks->state = 3;               // 进入等待双击状态
                }
            }
            break;
            
        // ========== 状态3：等待第二次单击（双击检测） ==========
        case 3:
            if(key_level == 0) {                 // 在超时前再次按下
                ks->state = 1;                   // 重新进入按下计时状态
                ks->press_time = current_time;   // 记录新的按下时刻
                ks->long_pressed = 0;            // 清空长按标志（因为重新开始计时）
            } else {                             // 仍然为释放状态
                // 检查是否超过双击间隔时间
                if(current_time - ks->release_time >= DOUBLE_CLICK_TIME) {
                    // 超时，视为短按一次
                    event = KEY_EVENT_SHORT;
                    ks->state = 0;               // 回到空闲
                    ks->click_count = 0;
                }
                // 若未超时，继续等待
            }
            break;
            
        default:
            ks->state = 0;                       // 未知状态，复位
            break;
    }
    
    return event;   // 返回检测到的事件（若无事件则返回 KEY_EVENT_NONE）
}
