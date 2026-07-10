#ifndef __KEYBOARD_H
#define __KEYBOARD_H
#include "include.h"

#define PASSWORD_MAX_LEN 6        // 密码最大长度
#define DEFAULT_PASSWORD "123456" // 默认密码

extern void keyboard_init(void);
extern char key_board_get(void);
extern void password_input_mode(void);        // 密码输入模式
extern void password_change_mode(void);       // 密码修改模式
extern void keyboard_test_mode(void);         // 键盘测试模式
extern char g_password[PASSWORD_MAX_LEN + 1]; // 存储密码

#endif
