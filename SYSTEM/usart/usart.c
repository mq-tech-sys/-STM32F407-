#include "sys.h"
#include "usart.h"

//加入以下代码,支持printf函数,而不需要选择use MicroLIB

#pragma import(__use_no_semihosting)
//标准库需要的支持函数
struct __FILE
{
    int handle;
};

FILE __stdout;

//定义_sys_exit()以避免使用半主机模式
void _sys_exit(int x)
{
    x = x;
}
//重定义fputc函数
int fputc(int ch, FILE *f)
{
    while ((USART1->SR & 0X40) == 0)
        ; //循环发送,直到发送完毕
    USART1->DR = (u8)ch;
    return ch;
}

void usart_send_string(USART_TypeDef *USARTx, char *str)
{
    while (*str)
    {
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USARTx, *str++);
    }
}

void usart_send_buf(USART_TypeDef *USARTx, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    for (i = 0; i < len; i++)
    {
        while (USART_GetFlagStatus(USARTx, USART_FLAG_TXE) == RESET)
            ;
        USART_SendData(USARTx, buf[i]);
    }
}

/* ==========================================================================
 * USART3 接收环形缓冲（单生产者-单消费者，无需加锁）
 *   生产者 = USART3 中断；消费者 = task_esp8266 / task_ble24
 *   head/tail 都是 uint16_t，Cortex-M4 上访问是原子的，不会撕裂
 * ========================================================================== */
#define UART3_RX_RING_SIZE 512

static volatile uint8_t  s_uart3_ring[UART3_RX_RING_SIZE];
static volatile uint16_t s_uart3_head = 0; /* 中断写 */
static volatile uint16_t s_uart3_tail = 0; /* 任务读 */
static volatile uint32_t s_uart3_drop = 0;

uint16_t uart3_read(uint8_t *dst, uint16_t max)
{
    uint16_t n = 0;

    if (dst == NULL || max == 0)
        return 0;

    while (n < max && s_uart3_head != s_uart3_tail)
    {
        dst[n++] = s_uart3_ring[s_uart3_tail];
        s_uart3_tail = (uint16_t)((s_uart3_tail + 1) % UART3_RX_RING_SIZE);
    }
    return n;
}

void uart3_rx_flush(void)
{
    s_uart3_tail = s_uart3_head;
}

uint32_t uart3_rx_dropped(void)
{
    return s_uart3_drop;
}

//初始化IO 串口1
//bound:波特率
void uart_init(u32 baud)
{
    //GPIO端口设置
    GPIO_InitTypeDef GPIO_InitStructure;
    USART_InitTypeDef USART_InitStructure;
    NVIC_InitTypeDef NVIC_InitStructure;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);  //使能GPIOA时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE); //使能USART1时钟

    //串口1对应引脚复用映射
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);  //GPIOA9复用为USART1
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_USART1); //GPIOA10复用为USART1

    //USART1端口配置
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10; //GPIOA9与GPIOA10
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;            //复用功能
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;       //速度50MHz
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;          //推挽复用输出
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;            //上拉
    GPIO_Init(GPIOA, &GPIO_InitStructure);                  //初始化PA9，PA10

    //USART1 初始化设置
    USART_InitStructure.USART_BaudRate = baud;                                      //波特率设置
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;                     //字长为8位数据格式
    USART_InitStructure.USART_StopBits = USART_StopBits_1;                          //一个停止位
    USART_InitStructure.USART_Parity = USART_Parity_No;                             //无奇偶校验位
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None; //无硬件数据流控制
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;                 //收发模式
    USART_Init(USART1, &USART_InitStructure);                                       //初始化串口1

    USART_Cmd(USART1, ENABLE); //使能串口1

    //USART_ClearFlag(USART1, USART_FLAG_TC);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE); //开启相关中断

    //Usart1 NVIC 配置
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;         //串口1中断通道
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3; //抢占优先级3
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;        //子优先级3
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;           //IRQ通道使能
    NVIC_Init(&NVIC_InitStructure);                           //根据指定的参数初始化VIC寄存器、
}

void usart3_init(void)
{
    uint32_t baud = 115200;
#if USART3_WIFI_USE
    baud = WIFI_BAUD;
#elif USART3_BLE_USE
    baud = BLE_BAUD;
#endif
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 | GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP; //改用上拉，避免信号不稳定
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource10, GPIO_AF_USART3);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource11, GPIO_AF_USART3);

    USART_InitStructure.USART_BaudRate = baud;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    USART_ITConfig(USART3, USART_IT_RXNE, ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART3, ENABLE);
}

//串口1中断服务程序
void USART1_IRQHandler(void)
{
    uint8_t d;

    if (USART_GetITStatus(USART1, USART_IT_RXNE) != RESET)
    {
        //接收串口数据
        d = USART_ReceiveData(USART1);

        printf("%c", d);

        //清空串口接收中断标志位
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void USART3_IRQHandler(void)
{
    uint8_t data;

    if (USART_GetITStatus(USART3, USART_IT_RXNE) == SET)
    {
        data = USART_ReceiveData(USART3);

        /* 只做一件事：把字节塞进环形缓冲。
         * 分帧（MQTT 报文边界 / BLE 的行边界）全部交给上层任务处理，
         * 这样中断里没有任何协议相关逻辑，也不会丢字节。 */
        {
            uint16_t next = (uint16_t)((s_uart3_head + 1) % UART3_RX_RING_SIZE);
            if (next != s_uart3_tail)
            {
                s_uart3_ring[s_uart3_head] = data;
                s_uart3_head = next;
            }
            else
            {
                s_uart3_drop++; //缓冲满，丢弃并计数
            }
        }

        USART_ClearITPendingBit(USART3, USART_IT_RXNE);
    }
}
