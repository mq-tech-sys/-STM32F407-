/**
  ******************************************************************************
  * @文件     ble.c
  * @摘要     蓝牙串口透传链路：把收到的字节按行切分后交给远程命令层
  *
  *   模块是串口透传型蓝牙（JDY-24 类），所以这里只负责"字节流 -> 行"的
  *   分帧，真正的命令解析与执行在 remote_cmd.c 里，和 WiFi/MQTT 链路共用同一份。
  ******************************************************************************
**/

#include "ble.h"

static char       s_line[BLE_LINE_MAX];
static uint8_t    s_len;
static TickType_t s_last_byte;

void ble24_init(void)
{
    s_len = 0;
    s_last_byte = xTaskGetTickCount();
    usart3_init();
}

static void flush_line(void)
{
    if (s_len == 0)
        return;
    s_line[s_len] = '\0';
    s_len = 0;
    remote_cmd_handle_line(s_line);
}

void ble24_task(void)
{
    uint8_t  buf[32];
    uint16_t n, i;

    n = uart3_read(buf, sizeof buf);
    for (i = 0; i < n; i++)
    {
        char c = (char)buf[i];

        s_last_byte = xTaskGetTickCount();

        if (c == '\r' || c == '\n')
        {
            flush_line();
            continue;
        }
        if (s_len >= BLE_LINE_MAX - 1)
            flush_line();                 /* 行太长，先处理掉再继续 */
        s_line[s_len++] = c;
    }

    /* 空闲超时兜底：手机端不发换行时也能识别出一条完整命令 */
    if (s_len > 0 &&
        ((int32_t)(xTaskGetTickCount() - s_last_byte)) > (int32_t)pdMS_TO_TICKS(BLE_IDLE_FLUSH_MS))
        flush_line();
}
