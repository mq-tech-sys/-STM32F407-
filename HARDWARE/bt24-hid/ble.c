#include "ble.h"

uint8_t g_ble_rx_buf[256];
uint32_t g_ble_cnt;

void ble24_init()
{
    usart3_init();
}

void ble24_send_str(char *str)
{
    usart_send_string(USART3, str);
}

// 蓝牙数据处理函数
void ble_process_loop(void)
{
    uint8_t cmd;
    uint8_t i;

    if (!g_usart_event)
    {
        return;
    }

    // 打印接收到的原始数据（十六进制）
    printf("蓝牙收到的消息(cnt=%d): ", g_ble_cnt);
    for(i = 0; i < g_ble_cnt; i++)
    {
        printf("%02X ", g_ble_rx_buf[i]);
    }
    printf("| String: [%s]\r\n", g_ble_rx_buf);

    // 只有收到的数据不少于 2 个字符才处理
    if(g_ble_cnt >= 2)
    {
        // 收到 "on" - 开锁
        if (strstr((char *)g_ble_rx_buf, "on") != NULL)
        {
            cmd = 1; // 开锁
            xQueueSend(g_servo_queue, &cmd, pdMS_TO_TICKS(10));
            printf("[BLE] Unlock command received!\r\n");
        }
        // 收到 "off" - 上锁
        else if (strstr((char *)g_ble_rx_buf, "off") != NULL)
        {
            cmd = 0; // 上锁
            xQueueSend(g_servo_queue, &cmd, pdMS_TO_TICKS(10));
            printf("[BLE] Lock command received!\r\n");
        }
        else
        {
            printf("[BLE] Unknown command!\r\n");
        }
    }
    else
    {
        printf("[BLE] Data too short, ignoring...\r\n");
    }

    // 清空缓冲区
    memset(g_ble_rx_buf, 0, sizeof(g_ble_rx_buf));
    g_ble_cnt = 0;
    g_usart_event = 0;
}

void ble_send_lock_state(uint8_t state)
{
    if (state)
    {
        ble24_send_str("on\r\n");
    }
    else
    {
        ble24_send_str("off\r\n");
    }
}
