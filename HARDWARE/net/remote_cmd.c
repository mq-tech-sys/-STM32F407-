/**
  ******************************************************************************
  * @文件     remote_cmd.c
  * @摘要     远程命令层实现（WiFi MQTT 与蓝牙 BLE 共用）
  ******************************************************************************
**/

#include "remote_cmd.h"

/* --------------------------------------------------------------------------
 * 通道输出：两个链路二选一，和 include.h 里的编译开关保持一致
 * -------------------------------------------------------------------------- */
void remote_cmd_send(const char *msg)
{
    if (msg == NULL)
        return;

#if USART3_WIFI_USE
    bemfa_mqtt_publish(msg);
#else
    usart_send_string(USART3, (char *)msg);
    usart_send_string(USART3, "\r\n");
#endif
}

/* --------------------------------------------------------------------------
 * 小工具
 * -------------------------------------------------------------------------- */

/** 判断 line 是否以 cmd 开头；若是，返回指向剩余部分的指针，否则返回 NULL */
static const char *match(const char *line, const char *cmd)
{
    size_t n = strlen(cmd);
    if (strncmp(line, cmd, n) == 0)
        return line + n;
    return NULL;
}

/** 把定长数字口令复制到 buf，返回 1 表示格式合法（非空且全是数字） */
static uint8_t copy_digits(const char *src, char *buf, size_t cap)
{
    size_t n = 0;

    while (src[n] != '\0' && src[n] != '\r' && src[n] != '\n' && src[n] != ' ')
        n++;
    if (n == 0 || n >= cap)
        return 0;
    for (size_t i = 0; i < n; i++)
    {
        if (src[i] < '0' || src[i] > '9')
            return 0;
        buf[i] = src[i];
    }
    buf[n] = '\0';
    return 1;
}

/** 去掉行首尾的空白与 \r \n，原地压缩 */
static void trim(char *line)
{
    size_t len, i = 0, j;

    while (line[i] == ' ' || line[i] == '\t' || line[i] == '\r' || line[i] == '\n')
        i++;
    if (i > 0)
    {
        len = strlen(line + i);
        for (j = 0; j <= len; j++)
            line[j] = line[i + j];
    }
    len = strlen(line);
    while (len > 0)
    {
        char c = line[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
            line[--len] = '\0';
        else
            break;
    }
}

/** 触发与某个物理按键相同的动作 */
static void fire_event(EventBits_t bits)
{
    if (g_event_group != NULL)
        xEventGroupSetBits(g_event_group, bits);
}

/** 请求舵机开/关锁 */
static void fire_servo(uint8_t cmd)
{
    if (g_servo_queue != NULL)
        xQueueSend(g_servo_queue, &cmd, 0);
}

/* --------------------------------------------------------------------------
 * 上报
 * -------------------------------------------------------------------------- */
void remote_cmd_report_state(void)
{
    remote_cmd_send(g_lock_state ? "state=unlock" : "state=lock");
}

static void report_counts(void)
{
    char buf[32];
    u16 n = 0;

    if (PS_ValidTempleteNum(&n) == 0)
        sprintf(buf, "num=fp,%u", (unsigned)n);
    else
        sprintf(buf, "num=fp,-1");
    remote_cmd_send(buf);

    sprintf(buf, "num=card,%u", (unsigned)g_card_count);
    remote_cmd_send(buf);
}

void remote_cmd_on_online(void)
{
    remote_cmd_report_state();
    report_counts();
    remote_cmd_send("ack=online");
}

/* --------------------------------------------------------------------------
 * 命令分发
 * -------------------------------------------------------------------------- */
#define CMD_LIST "unlock,close,state,fp_add,fp_del,fp_num,card_add,card_del,card_num,pwd:,pwd_set:,ping"

void remote_cmd_handle_line(char *line)
{
    const char *rest;
    char pwd[PASSWORD_MAX_LEN + 1];

    if (line == NULL)
        return;
    trim(line);
    if (line[0] == '\0')
        return;

    printf("[REMOTE] cmd=<%s>\r\n", line);

    /* ---------------- 开锁 / 上锁 ---------------- */
    if (strcmp(line, "unlock") == 0 || strcmp(line, "open") == 0 || strcmp(line, "on") == 0)
    {
        fire_servo(1);
        remote_cmd_send("ack=unlock");
        return;
    }
    if (strcmp(line, "close") == 0 || strcmp(line, "lock") == 0 || strcmp(line, "off") == 0)
    {
        fire_servo(0);
        remote_cmd_send("ack=close");
        return;
    }

    /* ---------------- 状态 / 数量 / 探测 ---------------- */
    if (strcmp(line, "state") == 0)
    {
        remote_cmd_report_state();
        return;
    }
    if (strcmp(line, "fp_num") == 0 || strcmp(line, "card_num") == 0)
    {
        report_counts();
        return;
    }
    if (strcmp(line, "ping") == 0)
    {
        remote_cmd_send("pong");
        return;
    }
    if (strcmp(line, "help") == 0)
    {
        remote_cmd_send("help=" CMD_LIST);
        return;
    }

    /* ---------------- 指纹 ---------------- */
    if (strcmp(line, "fp_add") == 0 || strcmp(line, "fp_enroll") == 0)
    {
        fire_event(EVENT_KEY1_SHORT);      /* 等同按键 1 短按：录入指纹 */
        remote_cmd_send("ack=fp_add");
        return;
    }
    if (strcmp(line, "fp_del") == 0)
    {
        fire_event(EVENT_KEY3_SHORT);      /* 等同按键 3 短按：清空指纹库 */
        remote_cmd_send("ack=fp_del");
        return;
    }

    /* ---------------- RFID 卡 ---------------- */
    if (strcmp(line, "card_add") == 0)
    {
        fire_event(EVENT_KEY1_DOUBLE);     /* 等同按键 1 双击：添加卡 */
        remote_cmd_send("ack=card_add");
        return;
    }
    if (strcmp(line, "card_del") == 0)
    {
        fire_event(EVENT_KEY2_DOUBLE);     /* 等同按键 2 双击：清空卡 */
        remote_cmd_send("ack=card_del");
        return;
    }

    /* ---------------- 密码开锁 ---------------- */
    rest = match(line, "pwd:");
    if (rest != NULL)
    {
        if (copy_digits(rest, pwd, sizeof pwd) && strcmp(pwd, g_password) == 0)
        {
            printf("[REMOTE] 密码校验通过\r\n");
            remote_cmd_send("ack=pwd_ok");
            fire_servo(1);
        }
        else
        {
            printf("[REMOTE] 密码校验失败\r\n");
            remote_cmd_send("ack=pwd_fail");
        }
        return;
    }

    /* ---------------- 修改密码 ---------------- */
    rest = match(line, "pwd_set:");
    if (rest != NULL)
    {
        if (copy_digits(rest, pwd, sizeof pwd))
        {
            strcpy(g_password, pwd);
            printf("[REMOTE] 密码已修改\r\n");
            remote_cmd_send("ack=pwd_set_ok");
        }
        else
        {
            remote_cmd_send("ack=pwd_set_bad");
        }
        return;
    }

    remote_cmd_send("err=unknown");
    remote_cmd_send("help=" CMD_LIST);
}
