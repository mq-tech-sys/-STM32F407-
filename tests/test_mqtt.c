/**
  ******************************************************************************
  * @文件     test_mqtt.c
  * @摘要     mqtt_packet.c 的 PC 端自测（无框架，靠 assert）
  *
  *   编译运行（Windows + MinGW）：
  *     gcc -std=c99 -Wall -Wextra -I../HARDWARE/net test_mqtt.c \
  *         ../HARDWARE/net/mqtt_packet.c -o test_mqtt.exe && test_mqtt.exe
  *   或直接用同目录的 run_tests.sh / run_tests.bat
  ******************************************************************************
**/

#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "mqtt_packet.h"

static int g_checks = 0;

#define CHECK(cond) do {                                                        \
		g_checks++;                                                             \
		if (!(cond)) {                                                          \
			printf("  [FAIL] %s:%d  %s\n", __FILE__, __LINE__, #cond);          \
			return 1;                                                           \
		}                                                                       \
	} while (0)

/* -------------------------------------------------------------------------- */
static int test_pingreq(void)
{
	uint8_t buf[8];
	size_t n = mqtt_encode_pingreq(buf, sizeof buf);

	CHECK(n == 2);
	CHECK(buf[0] == 0xC0 && buf[1] == 0x00);

	/* 解析回来 */
	mqtt_parser_t p;
	mqtt_parser_init(&p);
	CHECK(mqtt_parser_feed_byte(&p, 0xC0) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0x00) == 1);
	CHECK(p.msg.type == MQTT_TYPE_PINGREQ);
	CHECK(p.msg.blen == 0);
	printf("  pingreq                ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
static int test_connect(void)
{
	uint8_t buf[128];
	const char *cid = "28eccdb351e4bf84ad1390c143846844";   /* 32 字节，与巴法云私钥同形 */
	size_t n = mqtt_encode_connect(buf, sizeof buf, cid, NULL, NULL, 60);

	/* 固定头: 0x10 + 剩余长度(1 字节，因为 body < 128) */
	CHECK(n > 0);
	CHECK(buf[0] == 0x10);
	size_t expect_body = 10 + 2 + strlen(cid);
	CHECK((size_t)buf[1] == expect_body);
	CHECK(n == 1 + 1 + expect_body);

	/* 可变头: 协议名 + 级别 + 标志 + KeepAlive */
	CHECK(buf[2] == 0x00 && buf[3] == 0x04);
	CHECK(memcmp(&buf[4], "MQTT", 4) == 0);
	CHECK(buf[8] == 0x04);              /* 协议级别 3.1.1 */
	CHECK(buf[9] == 0x02);              /* CleanSession，无用户名/密码 */
	CHECK(buf[10] == 0x00 && buf[11] == 60);

	/* 载荷: clientID */
	CHECK(buf[12] == 0x00 && buf[13] == strlen(cid));
	CHECK(memcmp(&buf[14], cid, strlen(cid)) == 0);

	/* 带用户名密码时，标志位应为 0xC2，且长度相应增加 */
	/* cid="c1" user="appid" pass="sk" -> body = 10 + (2+2) + (2+5) + (2+2) = 25 */
	size_t n2 = mqtt_encode_connect(buf, sizeof buf, "c1", "appid", "sk", 30);
	CHECK(n2 == 1 + 1 + 25);
	CHECK(buf[9] == 0xC2);
	CHECK((size_t)buf[1] + 2 == n2);

	/* 只给密码不给用户名：必须自动补一个空用户名，标志位同样是 0xC2 */
	/* body = 10 + (2+2) + (2+0) + (2+2) = 20 */
	size_t n3 = mqtt_encode_connect(buf, sizeof buf, "c1", NULL, "sk", 30);
	CHECK(n3 == 1 + 1 + 20);
	CHECK(buf[9] == 0xC2);
	/* 载荷顺序: cid(2+2) -> 空 user(2+0) -> pass(2+2) */
	CHECK(buf[12] == 0x00 && buf[13] == 0x02 && memcmp(&buf[14], "c1", 2) == 0);
	CHECK(buf[16] == 0x00 && buf[17] == 0x00);          /* 空用户名 */
	CHECK(buf[18] == 0x00 && buf[19] == 0x02 && memcmp(&buf[20], "sk", 2) == 0);
	printf("  connect                ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
static int test_subscribe_publish_roundtrip(void)
{
	uint8_t buf[256];
	const char *topic = "lock001";
	const char *payload = "unlock";
	mqtt_parser_t p;

	/* ---- SUBSCRIBE ---- */
	size_t n = mqtt_encode_subscribe(buf, sizeof buf, 1, topic, 0);
	CHECK(n > 0);
	CHECK(buf[0] == 0x82);
	size_t off = 1 + ((buf[1] & 0x80) ? 2 : 1);
	CHECK(buf[off] == 0x00 && buf[off + 1] == 0x01);           /* 报文标识符 */
	CHECK(buf[off + 2] == 0x00 && buf[off + 3] == strlen(topic));
	CHECK(memcmp(&buf[off + 4], topic, strlen(topic)) == 0);
	CHECK(buf[off + 4 + strlen(topic)] == 0x00);               /* QoS0 */

	mqtt_parser_init(&p);
	{
		int r = 0;
		for (size_t i = 0; i < n; i++) {
			r = mqtt_parser_feed_byte(&p, buf[i]);
			if (i + 1 < n)
				CHECK(r == 0);
		}
		CHECK(r == 1);
	}
	CHECK(p.msg.type == MQTT_TYPE_SUBSCRIBE);
	CHECK(p.msg.flags == 0x02);

	/* ---- PUBLISH 往返 ---- */
	n = mqtt_encode_publish(buf, sizeof buf, topic,
	                        (const uint8_t *)payload, strlen(payload));
	CHECK(n > 0);
	CHECK(buf[0] == 0x30);                                     /* QoS0，不保留 */

	mqtt_parser_init(&p);
	{
		int r = 0;
		for (size_t i = 0; i < n; i++)
			r = mqtt_parser_feed_byte(&p, buf[i]);
		CHECK(r == 1);
	}
	CHECK(p.msg.type == MQTT_TYPE_PUBLISH);

	char got_topic[64];
	const uint8_t *got_payload = NULL;
	size_t got_len = 0;
	CHECK(mqtt_publish_split(&p.msg, got_topic, sizeof got_topic,
	                         &got_payload, &got_len) == 0);
	CHECK(strcmp(got_topic, topic) == 0);
	CHECK(got_len == strlen(payload));
	CHECK(memcmp(got_payload, payload, got_len) == 0);
	printf("  subscribe/publish      ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 拆包：一个字节一个字节喂 CONNACK */
static int test_connack_split(void)
{
	const uint8_t connack[] = { 0x20, 0x02, 0x00, 0x00 };
	mqtt_parser_t p;

	mqtt_parser_init(&p);
	CHECK(mqtt_parser_feed_byte(&p, connack[0]) == 0);
	CHECK(mqtt_parser_feed_byte(&p, connack[1]) == 0);
	CHECK(mqtt_parser_feed_byte(&p, connack[2]) == 0);
	CHECK(mqtt_parser_feed_byte(&p, connack[3]) == 1);
	CHECK(p.msg.type == MQTT_TYPE_CONNACK);
	CHECK(mqtt_connack_accepted(&p.msg) == 1);
	CHECK(mqtt_connack_code(&p.msg) == 0);

	/* 非 0 返回码：拒绝 */
	const uint8_t bad[] = { 0x20, 0x02, 0x00, 0x05 };
	mqtt_parser_init(&p);
	for (size_t i = 0; i < sizeof bad; i++)
		CHECK(mqtt_parser_feed_byte(&p, bad[i]) == (i + 1 == sizeof bad));
	CHECK(mqtt_connack_accepted(&p.msg) == 0);
	CHECK(mqtt_connack_code(&p.msg) == 5);
	printf("  connack split/badcode  ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 粘包：两帧连在一起喂，应该依次吐出两条 */
static int test_sticky_packets(void)
{
	uint8_t buf[64];
	size_t n = mqtt_encode_publish(buf, sizeof buf, "lock001",
	                               (const uint8_t *)"on", 2);
	CHECK(n > 0);
	/* 后面再拼一个 PINGREQ */
	uint8_t stream[80];
	memcpy(stream, buf, n);
	stream[n] = 0xC0;
	stream[n + 1] = 0x00;

	mqtt_parser_t p;
	mqtt_parser_init(&p);
	int got = 0;
	for (size_t i = 0; i < n + 2; i++) {
		int r = mqtt_parser_feed_byte(&p, stream[i]);
		if (r == 1) {
			got++;
			if (got == 1) {
				CHECK(p.msg.type == MQTT_TYPE_PUBLISH);
				char t[32];
				const uint8_t *pl;
				size_t pll;
				CHECK(mqtt_publish_split(&p.msg, t, sizeof t, &pl, &pll) == 0);
				CHECK(strcmp(t, "lock001") == 0);
				CHECK(pll == 2 && memcmp(pl, "on", 2) == 0);
			} else if (got == 2) {
				CHECK(p.msg.type == MQTT_TYPE_PINGREQ);
			}
		}
	}
	CHECK(got == 2);
	printf("  sticky packets         ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 剩余长度用满 2 字节（body >= 128），验证可变长整数编码 */
static int test_long_payload_varlen(void)
{
	uint8_t buf[300];
	uint8_t payload[200];

	for (size_t i = 0; i < sizeof payload; i++)
		payload[i] = (uint8_t)('a' + (i % 26));

	size_t n = mqtt_encode_publish(buf, sizeof buf, "t", payload, sizeof payload);
	CHECK(n > 0);
	/* body = 2 + 1 + 200 = 203，需 2 字节剩余长度：0xCB 0x01 */
	CHECK(buf[1] == 0xCB && buf[2] == 0x01);
	CHECK(n == 1 + 2 + 203);

	mqtt_parser_t p;
	mqtt_parser_init(&p);
	int r = 0;
	for (size_t i = 0; i < n; i++)
		r = mqtt_parser_feed_byte(&p, buf[i]);
	CHECK(r == 1);
	CHECK(p.msg.type == MQTT_TYPE_PUBLISH);
	CHECK(p.msg.blen == 203);

	char t[8];
	const uint8_t *pl = NULL;
	size_t pll = 0;
	CHECK(mqtt_publish_split(&p.msg, t, sizeof t, &pl, &pll) == 0);
	CHECK(strcmp(t, "t") == 0);
	CHECK(pll == sizeof payload);
	CHECK(memcmp(pl, payload, sizeof payload) == 0);
	printf("  varlen(203)            ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 非法报文：剩余长度字段超过 4 字节 -> 必须报错并复位，不能死循环/越界 */
static int test_bad_varlen(void)
{
	mqtt_parser_t p;
	mqtt_parser_init(&p);
	CHECK(mqtt_parser_feed_byte(&p, 0x30) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == -1);   /* 第 5 个续接字节 -> 非法 */

	/* 复位后能正常接收下一条 */
	CHECK(mqtt_parser_feed_byte(&p, 0xC0) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0x00) == 1);
	CHECK(p.msg.type == MQTT_TYPE_PINGREQ);
	printf("  bad varlen resync      ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 超长报文：声明的剩余长度超过 MQTT_MAX_PACKET -> 立即丢弃 */
static int test_oversize_rejected(void)
{
	mqtt_parser_t p;
	mqtt_parser_init(&p);
	CHECK(mqtt_parser_feed_byte(&p, 0x30) == 0);
	CHECK(mqtt_parser_feed_byte(&p, 0xFF) == 0);   /* 0x7F */
	CHECK(mqtt_parser_feed_byte(&p, 0x02) == -1);  /* 2*128 + 0x7F... 越界 */
	printf("  oversize rejected      ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
/** 缓冲容量不足时编码必须返回 0，不能越界写 */
static int test_encode_cap_guard(void)
{
	uint8_t small[4];

	CHECK(mqtt_encode_connect(small, sizeof small, "abcdef", NULL, NULL, 60) == 0);
	CHECK(mqtt_encode_subscribe(small, sizeof small, 1, "lock001", 0) == 0);
	CHECK(mqtt_encode_publish(small, sizeof small, "lock001",
	                          (const uint8_t *)"unlock", 6) == 0);
	CHECK(mqtt_encode_pingreq(small, sizeof small) == 2);
	CHECK(mqtt_encode_pingreq(small, 1) == 0);
	CHECK(mqtt_encode_publish(small, sizeof small, NULL, NULL, 0) == 0);
	CHECK(mqtt_encode_subscribe(small, sizeof small, 1, "", 0) == 0);
	printf("  cap guard              ok\n");
	return 0;
}

/* -------------------------------------------------------------------------- */
int main(void)
{
	printf("mqtt_packet self-test\n");
	int fails = 0;
	fails += test_pingreq();
	fails += test_connect();
	fails += test_subscribe_publish_roundtrip();
	fails += test_connack_split();
	fails += test_sticky_packets();
	fails += test_long_payload_varlen();
	fails += test_bad_varlen();
	fails += test_oversize_rejected();
	fails += test_encode_cap_guard();

	if (fails == 0)
		printf("\nALL PASS  (%d checks)\n", g_checks);
	else
		printf("\n%d TEST GROUP(S) FAILED\n", fails);
	return fails;
}
