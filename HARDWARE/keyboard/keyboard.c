#include "keyboard.h"

void keyboard_init(void)
{
    // 使能A、B、C、E、G端口
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA | RCC_AHB1Periph_GPIOB | RCC_AHB1Periph_GPIOC | RCC_AHB1Periph_GPIOE | RCC_AHB1Periph_GPIOG, ENABLE);

    // PB7,PB6,PA4,PA8行输出
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_High_Speed;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_15;
    GPIO_Init(GPIOG, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    // PG15,PC7,PC9,PE6为输入
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
    GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_Init(GPIOC, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_6;
    GPIO_Init(GPIOE, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}

char key_board_get(void)
{
    // PB7,PB6,PA4,PA8
    PBout(7) = 0;
    PAout(4) = 1;
    PGout(15) = 1;
    PCout(7) = 1;
    delay_ms(2);

    // PG15,PC7,PC9,PB6
    if (PCin(9) == 0)
        return '1';
    else if (PBin(6) == 0)
        return '2';
    else if (PEin(6) == 0)
        return '3';
    else if (PAin(8) == 0)
        return 'A';

    // PB7,PB6,PA4,PA8
    PBout(7) = 1;
    PAout(4) = 0;
    PGout(15) = 1;
    PCout(7) = 1;
    delay_ms(2);

    // PG15,PC7,PC9,PE6
    if (PCin(9) == 0)
        return '4';
    else if (PBin(6) == 0)
        return '5';
    else if (PEin(6) == 0)
        return '6';
    else if (PAin(8) == 0)
        return 'B';

    // PB7,PB6,PA4,PA8
    PBout(7) = 1;
    PAout(4) = 1;
    PGout(15) = 0;
    PCout(7) = 1;
    delay_ms(2);

    // PG15,PC7,PC9,PE6
    if (PCin(9) == 0)
        return '7';
    else if (PBin(6) == 0)
        return '8';
    else if (PEin(6) == 0)
        return '9';
    else if (PAin(8) == 0)
        return 'C';

    // PB7,PB6,PA4,PA8
    PBout(7) = 1;
    PAout(4) = 1;
    PGout(15) = 1;
    PCout(7) = 0;
    delay_ms(2);

    // PG15,PC7,PC9,PE6
    if (PCin(9) == 0)
        return '*';
    else if (PBin(6) == 0)
        return '0';
    else if (PEin(6) == 0)
        return '#';
    else if (PAin(8) == 0)
        return 'D';

    return 'E'; // 没有按键按下
}

char g_password[PASSWORD_MAX_LEN + 1] = DEFAULT_PASSWORD; // 全局密码变量

// 等待按键释放的辅助函数
static void wait_key_release(void)
{
    while (key_board_get() != 'E')
    {
        delay_ms(10);
    }
}

// 从矩阵键盘获取一个有效按键（带消抖）
static char get_key(void)
{
    char key;
    static uint32_t debug_cnt = 0;

    while (1)
    {
        key = key_board_get();
        if (key != 'E')
        {
            delay_ms(20); // 消抖
            if (key_board_get() == key)
            {
                printf("[DEBUG] Key pressed: %c\r\n", key);
                wait_key_release(); // 等待按键释放
                printf("[DEBUG] Key released\r\n");
                return key;
            }
        }
        delay_ms(10);

        // 定期打印调试信息
        debug_cnt++;
        if (debug_cnt % 100 == 0)
        {
            printf("[DEBUG] Waiting for key...\r\n");
        }
    }
}

// 显示掩码密码
static void display_mask_password(char *pwd, uint8_t len)
{
    uint8_t i;
    char mask_str[PASSWORD_MAX_LEN + 2] = {0};

    for (i = 0; i < len; i++)
    {
        mask_str[i] = '*';
    }
    mask_str[i] = '\0';

    oled_show_string(0, 4, (char *)mask_str);
}

// 密码输入函数
static void input_password(char *pwd_buf, uint8_t max_len)
{
    uint8_t index = 0;
    char key;

    printf("[DEBUG] Entering input_password mode\r\n");
    memset(pwd_buf, 0, max_len + 1);

    while (1)
    {
        key = get_key();
        printf("[DEBUG] Got key: %c, index: %d\r\n", key, index);

        // 数字键
        if (key >= '0' && key <= '9' && index < max_len)
        {
            pwd_buf[index++] = key;
            printf("[DEBUG] Adding digit: %c, password now: %s\r\n", key, pwd_buf);
            display_mask_password(pwd_buf, index);
        }
        // *键：删除
        else if (key == '*' && index > 0)
        {
            pwd_buf[--index] = '\0';
            printf("[DEBUG] Deleting, password now: %s\r\n", pwd_buf);
            display_mask_password(pwd_buf, index);
        }
        // #键：确认
        else if (key == '#')
        {
            printf("[DEBUG] Confirming password: %s\r\n", pwd_buf);
            break;
        }
    }
}

// 密码输入模式
void password_input_mode(void)
{
    uint8_t cmd;
    char input_pwd[PASSWORD_MAX_LEN + 1];

    printf("[DEBUG] ====== Entering password_input_mode ======\r\n");
    printf("[DEBUG] Current password: %s\r\n", g_password);

    OLED_Clear();
    oled_show_string(0, 0, "请输入密码:");

    input_password(input_pwd, PASSWORD_MAX_LEN);

    // 验证密码
    if (strcmp(input_pwd, g_password) == 0)
    {
        printf("密码正确!\r\n");
        OLED_Clear();
        oled_show_string(9, 4, "密码正确!");
        cmd = 1;
        xQueueSend(g_servo_queue, &cmd, NULL);
        delay_ms(1000);
    }
    else
    {
        printf("密码错误! 输入: %s, Expected: %s\r\n", input_pwd, g_password);
        OLED_Clear();
        oled_show_string(9, 4, "密码错误!");
        delay_ms(1000);
    }

    // 移除手动控制OLED的代码，让OLED任务根据g_lock_state自动更新
    printf("====== 输入密码 ======\r\n");
}

// 密码修改模式
void password_change_mode(void)
{
    char old_pwd[PASSWORD_MAX_LEN + 1];
    char new_pwd1[PASSWORD_MAX_LEN + 1];
    char new_pwd2[PASSWORD_MAX_LEN + 1];

    printf(" ====== 修改密码 ======\r\n");
    printf(" 现在密码: %s\r\n", g_password);

    // 输入旧密码
    OLED_Clear();
    oled_show_string(0, 0, "请输入旧密码:");
    printf("请输入旧密码：\r\n");
    input_password(old_pwd, PASSWORD_MAX_LEN);

    if (strcmp(old_pwd, g_password) != 0)
    {
        printf("旧密码错误! 输入: %s, Expected: %s\r\n", old_pwd, g_password);
        OLED_Clear();
        oled_show_string(9, 4, "旧密码错误!");
        delay_ms(1000);
        printf(" ====== Exiting password_change_mode (old pwd wrong) ======\r\n");
        return;
    }

    printf(" 旧密码正确!\r\n");

    // 输入新密码
    OLED_Clear();
    oled_show_string(0, 0, "请输入新密码:");
    printf("请输入新密码\r\n");
    input_password(new_pwd1, PASSWORD_MAX_LEN);

    // 再次输入新密码
    OLED_Clear();
    oled_show_string(0, 0, "请再次输入:");
    printf("请再次输入\r\n");
    input_password(new_pwd2, PASSWORD_MAX_LEN);

    // 验证两次输入是否一致
    if (strlen(new_pwd1) == 0)
    {
        OLED_Clear();
        oled_show_string(9, 4, "密码不能为空!");
        delay_ms(1000);
    }
    else if (strcmp(new_pwd1, new_pwd2) == 0)
    {
        strcpy(g_password, new_pwd1);
        param_save_password();
        printf("修改密码成功! 新密码: %s\r\n", g_password);
        OLED_Clear();
        oled_show_string(9, 4, "修改成功!");
        delay_ms(1000);
    }
    else
    {
        printf("两次输入不一致! 第一次: %s, 第二次: %s\r\n", new_pwd1, new_pwd2);
        OLED_Clear();
        oled_show_string(9, 4, "两次输入不一致!");
        delay_ms(1000);
    }

    printf("[DEBUG] ====== Exiting password_change_mode ======\r\n");
}

// 键盘测试模式
void keyboard_test_mode(void)
{
    char key;
    printf("[DEBUG] ====== Entering keyboard_test_mode ======\r\n");

    OLED_Clear();
    oled_show_string(0, 0, "键盘测试模式");
    oled_show_string(0, 2, "按任意键测试");

    while (1)
    {
        key = key_board_get();
        if (key != 'E')
        {
            printf("[KEYBOARD TEST] Key pressed: %c\r\n", key);
            OLED_Clear();
            oled_show_string(0, 0, "键盘测试模式");
            oled_show_string_fmt(0, 2, "按键: %c", key);
            delay_ms(500);

            // 按D键退出测试
            if (key == 'D')
            {
                printf("[KEYBOARD TEST] Exiting test mode...\r\n");
                break;
            }

            OLED_Clear();
            oled_show_string(0, 0, "键盘测试模式");
            oled_show_string(0, 2, "按任意键测试");
        }
        delay_ms(10);
    }

    printf("[DEBUG] ====== Exiting keyboard_test_mode ======\r\n");
}

void key_board_test(void)
{
    uint32_t key_sta = 0;
    char key_old = 0;
    char key_cur = 0;

    /* 使用状态机思想得到按键的状态 */
    switch (key_sta)
    {
    case 0: // 获取按下的按键
    {

        key_cur = key_board_get();

        if (key_cur != 'E')
        {
            key_old = key_cur;
            key_sta = 1;
        }
    }
    break;

    case 1: // 确认按下的按键
    {

        key_cur = key_board_get();

        if ((key_cur != 'E') && (key_cur == key_old))
        {
            printf("KEY %c Down\r\n", key_cur);

            key_sta = 2;
        }
    }
    break;

    case 2: // 获取释放的按键
    {

        key_cur = key_board_get();

        if (key_cur == 'E')
        {
            printf("KEY %c Up\r\n", key_old);

            key_sta = 0;

            key_old = 'E';
        }
    }
    break;

    default:
        break;
    }
}
