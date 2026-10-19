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

/**
 * @brief  收到的内容是不是"设备自己会发出的应答"
 * @note   同一个主题上如果有两台设备（或 broker 把消息回投给发布者），
 *         这些应答会被当成命令收回来。一旦对它们回 err=unknown + help=，
 *         两台设备就会互刷成死循环，很快把流量和内存打满。
 */
static uint8_t is_reply_line(const char *line)
{
    static const char *const prefixes[] = {
        "ack=", "state=", "num=", "help=", "err=",
        "fp=", "fpend=", "card=", "cardend="
    };
    size_t i;

    for (i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
    {
        if (strncmp(line, prefixes[i], strlen(prefixes[i])) == 0)
            return 1;
    }
    return (strcmp(line, "pong") == 0) ? 1 : 0;
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

/** 解析一个十进制整数；后面只允许空白或结束 */
static uint8_t parse_uint(const char *src, unsigned *out)
{
    unsigned v = 0;
    size_t n = 0;

    while (src[n] >= '0' && src[n] <= '9' && n < 5)
    {
        v = v * 10 + (unsigned)(src[n] - '0');
        n++;
    }
    if (n == 0)
        return 0;
    if (src[n] != '\0' && src[n] != ' ' && src[n] != '\r' && src[n] != '\n')
        return 0;

    *out = v;
    return 1;
}

/** 取功能锁（不等待）。指纹/卡片的读写不能和录入、刷卡流程并发 */
static uint8_t take_function_lock(void)
{
    if (g_mutex_function == NULL)
        return 1;
    return (xSemaphoreTake(g_mutex_function, 0) == pdTRUE) ? 1 : 0;
}

static void give_function_lock(void)
{
    if (g_mutex_function != NULL)
        xSemaphoreGive(g_mutex_function);
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
#define CMD_LIST "unlock,close,state,fp_add,fp_del,fp_del:<id>,fp_list,fp_num,"\
                 "card_add,card_del,card_del:<idx>,card_list,card_num,"\
                 "pwd:<6>,pwd_set:<6>,ping"

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

    /* 自己（或另一台同主题设备）发的回包被投回来了，不能再回一次。
     * 否则 err=unknown 和 help=... 会互相触发，形成无限循环。 */
    if (is_reply_line(line))
    {
        printf("[REMOTE] 忽略回投的应答: %s\r\n", line);
        return;
    }

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

    /* ---------------- 列出指纹（一行一个 ID，fpend=<个数> 收尾） ----------------
     * AS608 没有"列出模板"的命令，只能从 0 开始逐个 LoadChar 探测：
     * 确认码 0x00 = 该编号有指纹，其它 = 空位。
     */
    if (strcmp(line, "fp_list") == 0)
    {
        u16 i, n = 0, total = 0;
        char buf[24];

        if (!take_function_lock())
        {
            remote_cmd_send("err=busy");
            return;
        }

        if (PS_ValidTempleteNum(&total) != 0)
            total = 0;
        if (total > fp_capacity())
            total = fp_capacity();

        /* 找齐 total 个就提前结束，避免每次都扫满整个容量 */
        for (i = 0; i < fp_capacity() && n < total; i++)
        {
            if (fp_has_id(i))
            {
                sprintf(buf, "fp=%u", (unsigned)i);
                remote_cmd_send(buf);
                n++;
            }
        }

        sprintf(buf, "fpend=%u", (unsigned)n);
        remote_cmd_send(buf);
        give_function_lock();
        return;
    }

    /* ---------------- 删除指定 ID 的指纹 ---------------- */
    rest = match(line, "fp_del:");
    if (rest != NULL)
    {
        unsigned id;

        if (!parse_uint(rest, &id))
        {
            remote_cmd_send("ack=fp_del_bad");
            return;
        }
        if (!take_function_lock())
        {
            remote_cmd_send("err=busy");
            return;
        }

        if (fp_delete_id((u16)id))
        {
            printf("[REMOTE] 已删除指纹 ID=%u\r\n", id);
            remote_cmd_send("ack=fp_del_ok");
        }
        else
        {
            remote_cmd_send("ack=fp_del_fail");
        }
        give_function_lock();
        return;
    }

    /* ---------------- 列出卡片（card=<索引>,<UID>，cardend=<数量> 收尾） ---------------- */
    if (strcmp(line, "card_list") == 0)
    {
        uint8_t i;
        char buf[32];

        for (i = 0; i < g_card_count && i < 10; i++)
        {
            sprintf(buf, "card=%u,%02X%02X%02X%02X",
                    (unsigned)i,
                    g_card_list[i].uid[0], g_card_list[i].uid[1],
                    g_card_list[i].uid[2], g_card_list[i].uid[3]);
            remote_cmd_send(buf);
        }

        sprintf(buf, "cardend=%u", (unsigned)g_card_count);
        remote_cmd_send(buf);
        return;
    }

    /* ---------------- 删除指定索引的卡 ---------------- */
    rest = match(line, "card_del:");
    if (rest != NULL)
    {
        unsigned idx;

        if (!parse_uint(rest, &idx) || idx > 255)
        {
            remote_cmd_send("ack=card_del_bad");
            return;
        }
        if (delete_card_by_index((uint8_t)idx))
            remote_cmd_send("ack=card_del_ok");
        else
            remote_cmd_send("ack=card_del_fail");
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
            param_save_password();
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
