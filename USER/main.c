/****************************************************************
*    项目:基于STM32F407的智能指纹门锁
*    作者:叶铭强
*    硬件:STM32F407ZET6 + AS608指纹 + MFRC522(RFID) + ESP8266/蓝牙 + 0.96"OLED + SG90舵机
*    说明:
*	1.独立按键（短按/长按/双击）
*		短按  按键1=录入指纹   按键2=刷指纹     按键3=删除全部指纹
*		双击  按键1=添加RFID卡 按键2=删除全部卡
*		长按  按键1=密码开锁   按键2=修改密码
*	2.4x4矩阵键盘（密码输入 / 密码修改 / 键盘测试）
*		* = 退格   # = 确认提交
*	3.开锁方式
*		1)指纹开锁
*		2)密码开锁
*		3)RFID刷卡开锁（自动轮询，仅已登记的卡有效）
*		4)WiFi(巴法云) / 蓝牙 远程开锁，由 USART3_WIFI_USE / USART3_BLE_USE 二选一
*	4.任一时刻只允许一个"操作型"功能占用（g_mutex_function），
*		开锁动作统一走 g_servo_queue 队列，串行驱动舵机
*****************************************************************/
#include "include.h"

// 全局变量
GPIO_InitTypeDef GPIO_InitStructure;
NVIC_InitTypeDef NVIC_InitStructure;
USART_InitTypeDef USART_InitStructure;
EXTI_InitTypeDef EXTI_InitStructure;
TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure;
TIM_OCInitTypeDef TIM_OCInitStructure;

static TaskHandle_t app_task_init = NULL;
static TaskHandle_t app_task_oled = NULL;
static TaskHandle_t app_task_servo = NULL;
static TaskHandle_t app_task_esp8266 = NULL;
static TaskHandle_t app_task_as608 = NULL;
static TaskHandle_t app_task_key = NULL;
static TaskHandle_t app_task_keyboard = NULL;
static TaskHandle_t app_task_ble24 = NULL;
static TaskHandle_t app_task_rfid = NULL;
static TaskHandle_t app_task_rfid_unlock = NULL;
/* 消息队列 */
QueueHandle_t g_servo_queue;

/* 互斥信号量 */
SemaphoreHandle_t g_mutex_function; // 功能互斥锁保证一个时刻只有一个任务在执行
SemaphoreHandle_t g_mutex_printf;
SemaphoreHandle_t g_mutex_rfid;
/* 事件标志组 */
EventGroupHandle_t g_event_group;

CARD_UID g_card_list[10] = {0};
uint8_t g_card_count = 0;

uint8_t g_lock_state = 0;

/* 初始化任务 */
static void task_init(void *pvParameters);

/* oled任务 */
static void task_oled(void *pvParameters);

/* wifi任务 */
static void task_esp8266(void *pvParameters);

/* 舵机任务 */
static void task_servo(void *pvParameters);

/* 指纹任务 */
static void task_as608(void *pvParameters);

/* 按键任务 */
static void task_key(void *pvParameters);

/* 4*4按键任务 */
static void task_keyboard(void *pvParameters);

/* 蓝牙任务 */
static void task_ble24(void *pvParameters);

/* RFID任务 */
static void task_rfid(void *pvParameters);
static void task_rfid_unlock(void *pvParameters);
// 安全打印
void dgb_printf_safe(const char *format, ...)
{

	va_list args;
	va_start(args, format);

	/* 获取互斥信号量 */
	xSemaphoreTake(g_mutex_printf, portMAX_DELAY);

	vprintf(format, args);

	/* 释放互斥信号量 */
	xSemaphoreGive(g_mutex_printf);

	va_end(args);
}
int main(void)
{
	/* 调试串口波特率由 USART3 用途决定 */
	uint32_t baud = 115200;
#if USART3_WIFI_USE
	baud = WIFI_BAUD;
#elif USART3_BLE_USE
	baud = BLE_BAUD;
#endif
	/* 系统初始化 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	SysTick_Config(SystemCoreClock / configTICK_RATE_HZ);
	uart_init(baud);

	/* 创建初始化任务 */
	xTaskCreate((TaskFunction_t)task_init,
				(const char *)"task_init",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_init);

	/*开启调度器*/
	vTaskStartScheduler();

	while (1)
		;
}

static void task_init(void *pvParameters)
{
	/* 创建消息队列 */
	g_servo_queue = xQueueCreate(5, 1);

	/* 创建互斥锁 */
	g_mutex_function = xSemaphoreCreateMutex(); // 功能互斥锁
	g_mutex_printf = xSemaphoreCreateMutex();
	g_mutex_rfid = xSemaphoreCreateMutex();
	/* 创建事件标志组 */
	g_event_group = xEventGroupCreate();

	/* 初始化硬件 */
	OLED_Init();
#if USART3_WIFI_USE
	esp8266_init();
#elif USART3_BLE_USE
	ble24_init();
#endif
	servo_init();
	Beep_Init();
	key_init();
	usart2_init(42, usart2_baund); // 初始化串口2,用于指纹模块通信
	PS_StaGPIO_Init();			   // 初始化FR状态引脚
	keyboard_init();
	MFRC522_Initializtion();
	OLED_Clear();

	/*创建任务*/
	/* 创建oled任务 */
	xTaskCreate((TaskFunction_t)task_oled,
				(const char *)"task_oled",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_oled);

	/* 创建舵机任务 */
	xTaskCreate((TaskFunction_t)task_servo,
				(const char *)"task_servo",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_servo);
#if USART3_WIFI_USE
	/* 创建wifi任务 */
	xTaskCreate((TaskFunction_t)task_esp8266,
				(const char *)"task_esp8266",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_esp8266);
#elif USART3_BLE_USE
	/* 创建蓝牙任务 */
	xTaskCreate((TaskFunction_t)task_ble24,
				(const char *)"task_ble24",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_ble24);
#endif
	/* 创建指纹模块任务 */
	xTaskCreate((TaskFunction_t)task_as608,
				(const char *)"task_as608",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_as608);

	/* 创建按键任务 */
	xTaskCreate((TaskFunction_t)task_key,
				(const char *)"task_key",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_key);

	/* 创建4*4按键任务 */
	xTaskCreate((TaskFunction_t)task_keyboard,
				(const char *)"task_keyboard",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_keyboard);

	/* 创建rfid任务 */
	xTaskCreate((TaskFunction_t)task_rfid,
				(const char *)"task_rfid",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_rfid);
	/* 创建rfid刷卡任务 */
	xTaskCreate((TaskFunction_t)task_rfid_unlock,
				(const char *)"task_rfid_unlock",
				(uint16_t)512,
				(void *)NULL,
				(UBaseType_t)5,
				(TaskHandle_t *)&app_task_rfid_unlock);
	vTaskDelete(NULL);
}

/*oled任务*/
static void task_oled(void *pvParameters)
{
	uint8_t last = 0xFF;
	for (;;)
	{
		if (g_lock_state != last)
		{
			OLED_Clear();
			if (g_lock_state == 1)
				OLED_DrawBMP(48, 2, 80, 6, g_image_dot_tbl[3].address);
			else
				OLED_DrawBMP(48, 2, 80, 6, g_image_dot_tbl[2].address);
			last = g_lock_state;
		}
		vTaskDelay(100);
	}
}

#if USART3_WIFI_USE
/*esp8266任务*/
static void task_esp8266(void *pvParameters)
{
	TickType_t last_heartbeat;
	esp8266_connect();
	last_heartbeat = xTaskGetTickCount();
	for (;;)
	{
		// 每10ms处理一次巴法云消息，控制响应
		esp8266_bafa_loop();
		vTaskDelay(10);

		// 每60秒发送一次心跳
		if ((xTaskGetTickCount() - last_heartbeat) >= 60000)
		{
			esp8266_bafa_send_heartbeat();
			last_heartbeat = xTaskGetTickCount();
		}
	}
}
#elif USART3_BLE_USE
/*蓝牙任务*/
static void task_ble24(void *pvParameters)
{
	for (;;)
	{
		// 每10ms处理一次蓝牙消息
		ble_process_loop();
		vTaskDelay(10);
	}
}
#endif

/*舵机任务*/
static void task_servo(void *pvParameters)
{
	uint8_t cmd;
	uint8_t found_valid;
	for (;;)
	{
		found_valid = 0;

		// 一直阻塞等待，直到找到有效的指令！
		while (xQueueReceive(g_servo_queue, &cmd, (found_valid ? 0 : portMAX_DELAY)) == pdPASS)
		{
			if (cmd == 1 || cmd == 0)
			{
				found_valid = 1;
				break; // 找到有效的指令！
			}
		}

		if (found_valid)
		{
			if (cmd == 1) // 开锁
			{
				servo_angle(90);
				Beep_Hint(); // 蜂鸣器提示音
				vTaskDelay(300);
				g_lock_state = 1;
				
				// 上报门锁状态为开
#if USART3_WIFI_USE
				esp8266_bafa_send_lock_state(1);
#elif USART3_BLE_USE
				ble_send_lock_state(1);
#endif

				// 2秒后自动上锁
				vTaskDelay(2000);

				servo_angle(0);
				g_lock_state = 0;
				
				// 上报门锁状态为关
#if USART3_WIFI_USE
				esp8266_bafa_send_lock_state(0);
#elif USART3_BLE_USE
				ble_send_lock_state(0);
#endif
			}
			else if (cmd == 0) // 关锁
			{
				servo_angle(0);
				vTaskDelay(300);
				g_lock_state = 0;
				
				// 上报门锁状态为关
#if USART3_WIFI_USE
				esp8266_bafa_send_lock_state(0);
#elif USART3_BLE_USE
				ble_send_lock_state(0);
#endif
			}
		}
	}
}

/*指纹模块任务*/
static void task_as608(void *pvParameters)
{
	EventBits_t eb;
	for (;;)
	{
		eb = xEventGroupWaitBits(g_event_group, EVENT_KEY1_SHORT | EVENT_KEY2_SHORT | EVENT_KEY3_SHORT | EVENT_KEY1_DOUBLE | EVENT_KEY2_DOUBLE | EVENT_KEY3_DOUBLE,
								 pdTRUE, pdFALSE, portMAX_DELAY);

		// 尝试获取功能互斥锁，获取失败说明其他功能正在执行，直接跳过
		if (xSemaphoreTake(g_mutex_function, 0) == pdTRUE)
		{
			if (eb & EVENT_KEY1_SHORT) // 1采集指纹
			{
				Add_FR();
			}
			else if (eb & EVENT_KEY2_SHORT) // 2刷指纹
			{
				press_FR();
			}
			else if (eb & EVENT_KEY3_SHORT) // 3删除指纹
			{
				Del_FR();
			}
			// 释放互斥锁
			xSemaphoreGive(g_mutex_function);
		}

		vTaskDelay(10);
	}
}

/*rfid任务*/
static void task_rfid(void *pvParameters)
{
	EventBits_t eb;
	for (;;)
	{
		eb = xEventGroupWaitBits(g_event_group, EVENT_KEY1_DOUBLE | EVENT_KEY2_DOUBLE,
								 pdTRUE, pdFALSE, portMAX_DELAY);

		// 尝试获取功能互斥锁，获取失败说明其他功能正在执行，直接跳过
		if (xSemaphoreTake(g_mutex_function, portMAX_DELAY) == pdTRUE)
		{
			if (eb & EVENT_KEY1_DOUBLE) // 添加卡
			{
				add_card();
			}
			else if (eb & EVENT_KEY2_DOUBLE) // 删除所有卡
			{
				delete_all_cards();
			}
			// 释放互斥锁
			xSemaphoreGive(g_mutex_function);
		}
	}
}

/*rfid刷卡任务*/
static void task_rfid_unlock(void *pvParameters)
{
	for (;;)
	{
		// 尝试获取功能互斥锁，获取成功后才允许执行刷卡解锁
		if (xSemaphoreTake(g_mutex_function, 0) == pdTRUE)
		{
			check_card_unlock();
			xSemaphoreGive(g_mutex_function);
		}

		vTaskDelay(pdMS_TO_TICKS(100)); // 100ms刷一次
	}
}

/*4*4按键任务*/
static void task_keyboard(void *pvParameters)
{
	EventBits_t eb;
	for (;;)
	{
		eb = xEventGroupWaitBits(g_event_group, EVENT_KEY1_LONG | EVENT_KEY2_LONG | EVENT_KEY3_LONG,
								 pdTRUE, pdFALSE, portMAX_DELAY);

		// 尝试获取功能互斥锁，获取失败说明其他功能正在执行，直接跳过
		if (xSemaphoreTake(g_mutex_function, 0) == pdTRUE)
		{
			if (eb & EVENT_KEY1_LONG)
			{
				password_input_mode();
			}
			else if (eb & EVENT_KEY2_LONG)
			{
				password_change_mode();
			}
			// 释放互斥锁
			xSemaphoreGive(g_mutex_function);
		}
		vTaskDelay(100);
	}
}

/*按键任务*/
static void task_key(void *pvParameters)
{
	uint8_t event;
	static uint32_t counter = 0;

	for (;;)
	{
		// 扫描按键1
		event = key_scan(KEY_ID_1);
		if (event == KEY_EVENT_SHORT)
		{
			printf("[KEY] KEY1 SHORT pressed!\r\n");
			xEventGroupSetBits(g_event_group, EVENT_KEY1_SHORT);
		}
		else if (event == KEY_EVENT_LONG)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY1_LONG);
		}
		else if (event == KEY_EVENT_DOUBLE)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY1_DOUBLE);
		}

		// 扫描按键2
		event = key_scan(KEY_ID_2);
		if (event == KEY_EVENT_SHORT)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY2_SHORT);
		}
		else if (event == KEY_EVENT_LONG)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY2_LONG);
		}
		else if (event == KEY_EVENT_DOUBLE)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY2_DOUBLE);
		}

		// 扫描按键3
		event = key_scan(KEY_ID_3);
		if (event == KEY_EVENT_SHORT)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY3_SHORT);
		}
		else if (event == KEY_EVENT_LONG)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY3_LONG);
		}
		else if (event == KEY_EVENT_DOUBLE)
		{
			xEventGroupSetBits(g_event_group, EVENT_KEY3_DOUBLE);
		}

		vTaskDelay(10);
	}
}

void vApplicationMallocFailedHook(void)
{
	while (1)
		;
}
void vApplicationIdleHook(void) {}
void vApplicationStackOverflowHook(TaskHandle_t t, char *n)
{
	while (1)
		;
}
void vApplicationTickHook(void) {}
