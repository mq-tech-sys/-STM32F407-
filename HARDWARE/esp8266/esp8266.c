#include "esp8266.h"

uint8_t g_esp8266_rx_buf[256];
uint32_t g_esp8266_cnt;


void esp8266_init(void)
{
    usart3_init();
}

void esp8266_send_str(char *str)
{
    usart_send_string(USART3, str);
}



void esp8266_connect(void)
{
    char cmd[128];

    usart_send_string(USART3, "+++");
    delay_ms(1000);

    usart_send_string(USART3, "AT+RST\r\n");
    delay_ms(3000);

    usart_send_string(USART3, "AT+CWJAP_DEF=\"" WIFI_NAME "\",\"" WIFI_PASSWORD "\"\r\n");
    delay_ms(8000);

    usart_send_string(USART3, "AT+CIPSTART=\"TCP\",\"tcp.bemfa.com\",8344\r\n");
    delay_ms(3000);

    usart_send_string(USART3, "AT+CIPMODE=1\r\n");
    delay_ms(1000);

    usart_send_string(USART3, "AT+CIPSEND\r\n");
    delay_ms(1000);

    sprintf(cmd, "cmd=1&uid=%s&topic=%s\r\n", BAFA_UID, BAFA_TOPIC_LOCK);
    usart_send_string(USART3, cmd);
}

void esp8266_bafa_loop(void)
{
    char *p = NULL;
    char *topic = NULL;
    char *msg = NULL;
    uint8_t cmd;

    if (!g_usart_event)
    {
        return;
    }

    p = strstr((char *)g_esp8266_rx_buf, "topic=");
    if (p) topic = p + 6;

    p = strstr((char *)g_esp8266_rx_buf, "msg=");
    if (p) msg = p + 4;

    if (topic && msg)
    {
        if (strstr(topic, BAFA_TOPIC_LOCK))
        {
            if (strstr(msg, "on"))
            {
                cmd = 1;  // 开锁
                xQueueSend(g_servo_queue, &cmd, pdMS_TO_TICKS(10));
            }
            else if (strstr(msg, "off"))
            {
                cmd = 0;  // 上锁
                xQueueSend(g_servo_queue, &cmd, pdMS_TO_TICKS(10));
            }
        }
    }

    memset(g_esp8266_rx_buf, 0, sizeof(g_esp8266_rx_buf));
    g_esp8266_cnt = 0;
    g_usart_event = 0;
}

int32_t esp8266_bafa_send_heartbeat(void)
{
    esp8266_send_str("ping\r\n");
    return 0;
}

void esp8266_bafa_send_lock_state(uint8_t state)
{
    char cmd[128];
    sprintf(cmd, "cmd=2&uid=%s&topic=%s&msg=%s\r\n", 
            BAFA_UID, BAFA_TOPIC_LOCK, state ? "on" : "off");
    esp8266_send_str(cmd);
}
