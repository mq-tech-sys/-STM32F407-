#ifndef __ESP8266_H
#define __ESP8266_H
                    
#include "include.h"

#define WIFI_NAME "ymq"
#define WIFI_PASSWORD "88888888"
#define BAFA_UID "28eccdb351e4bf84ad1390c143846844" 

#define BAFA_SERVER "bemfa.com"
#define BAFA_PORT 8344

#define BAFA_TOPIC_LOCK "lock001"


extern uint8_t g_esp8266_rx_buf[256];
extern uint32_t g_esp8266_cnt;
extern void esp8266_init(void); 
extern void esp8266_connect(void);
extern void esp8266_bafa_process_message(void);
extern int32_t esp8266_bafa_send_heartbeat(void);
extern void esp8266_bafa_loop(void);
extern void esp8266_bafa_send_lock_state(uint8_t state);
#endif
