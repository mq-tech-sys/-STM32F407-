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

// �������ݴ�������
void ble_process_loop(void)
{
    uint8_t cmd;
    uint8_t i;

    if (!g_usart_event)
    {
        return;
    }

    // ��ӡ���յ���ԭʼ���ݣ�ʮ��������ʽ��
    printf("�������յ���Ϣ(cnt=%d): ", g_ble_cnt);
    for(i = 0; i < g_ble_cnt; i++)
    {
        printf("%02X ", g_ble_rx_buf[i]);
    }
    printf("| String: [%s]\r\n", g_ble_rx_buf);

    // ֻ�н��յ�����2���ַ��Ŵ���
    if(g_ble_cnt >= 2)
    {
        // ����Ƿ��յ� "on" - ����
        if (strstr((char *)g_ble_rx_buf, "on") != NULL)
        {
            cmd = 1; // ����
            xQueueSend(g_servo_queue, &cmd, pdMS_TO_TICKS(10));
            printf("[BLE] Unlock command received!\r\n");
        }
        // ����Ƿ��յ� "off" - ����
        else if (strstr((char *)g_ble_rx_buf, "off") != NULL)
        {
            cmd = 0; // ����
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
