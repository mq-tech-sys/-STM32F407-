/**
  ******************************************************************************
  * @文件     mqtt_packet.c
  * @摘要     MQTT 3.1.1 控制报文的编码与流式解码实现
  ******************************************************************************
**/

#include "mqtt_packet.h"

/* ==========================================================================
 * 小工具
 * ========================================================================== */

static size_t str_len_capped(const char *s, size_t cap)
{
	size_t n = 0;
	if (s == NULL)
		return 0;
	while (n < cap && s[n] != '\0')
		n++;
	return n;
}

/** 写"2 字节大端长度 + 内容"，返回写入字节数；cap 不够返回 0 */
static size_t put_len_str(uint8_t *buf, size_t cap, const char *s)
{
	size_t n = str_len_capped(s, 0xFFFF);
	if (cap < n + 2 || n > 0xFFFF)
		return 0;
	buf[0] = (uint8_t)(n >> 8);
	buf[1] = (uint8_t)(n & 0xFF);
	for (size_t i = 0; i < n; i++)
		buf[2 + i] = (uint8_t)s[i];
	return n + 2;
}

/** 把"剩余长度"按可变长整数写入 buf[1..4]，返回字节数 */
static size_t put_remaining_length(uint8_t *buf, uint32_t len)
{
	size_t n = 0;
	do {
		uint8_t byte = (uint8_t)(len % 128);
		len /= 128;
		if (len > 0)
			byte |= 0x80;		/* 后续还有字节 */
		buf[n++] = byte;
	} while (len > 0 && n < 4);
	return n;
}

/** 组装固定头（类型 + 可变长剩余长度），返回固定头总字节数 */
static size_t put_fixed_header(uint8_t *buf, size_t cap,
                               uint8_t type_flags, uint32_t body_len)
{
	uint8_t rl[4];
	size_t rn;

	if (cap < 5)
		return 0;
	rn = put_remaining_length(rl, body_len);
	if (body_len > 0x7F && rn < 2)
		return 0;				/* 理论不可达，防御 */

	buf[0] = type_flags;
	for (size_t i = 0; i < rn; i++)
		buf[1 + i] = rl[i];
	return 1 + rn;
}

/* ==========================================================================
 * 编码
 * ========================================================================== */

size_t mqtt_encode_connect(uint8_t *buf, size_t cap,
                           const char *client_id,
                           const char *user, const char *pass,
                           uint16_t keepalive_s)
{
	size_t hlen, off = 0, n;
	uint8_t flags = 0x02;		/* bit1 = Clean Session */

	if (buf == NULL || client_id == NULL)
		return 0;

	/* 先算 body 长度：可变头 10 字节 + clientID + [user] + [pass] */
	size_t id_len = str_len_capped(client_id, 0xFFFF);
	size_t body = 10 + 2 + id_len;
	if (user != NULL && user[0] != '\0') {
		size_t ul = str_len_capped(user, 0xFFFF);
		body += 2 + ul;
		flags |= 0x80;
	}
	/* MQTT 规定：带密码就必须带用户名；没有用户名时用空串占位 */
	if (pass != NULL && pass[0] != '\0') {
		size_t pl = str_len_capped(pass, 0xFFFF);
		if ((flags & 0x80) == 0)
			body += 2;		/* 空用户名 */
		body += 2 + pl;
		flags |= 0xC0;
	}

	hlen = put_fixed_header(buf, cap, MQTT_TYPE_CONNECT, (uint32_t)body);
	if (hlen == 0 || cap < hlen + body)
		return 0;
	off = hlen;

	/* 可变头 */
	buf[off++] = 0x00;
	buf[off++] = 0x04;
	buf[off++] = 'M';
	buf[off++] = 'Q';
	buf[off++] = 'T';
	buf[off++] = 'T';
	buf[off++] = 0x04;					/* 协议级别 4 = 3.1.1 */
	buf[off++] = flags;
	buf[off++] = (uint8_t)(keepalive_s >> 8);
	buf[off++] = (uint8_t)(keepalive_s & 0xFF);

	/* 载荷：客户端 ID */
	n = put_len_str(&buf[off], cap - off, client_id);
	if (n == 0)
		return 0;
	off += n;

	if ((flags & 0x80) != 0) {
		n = put_len_str(&buf[off], cap - off, (user != NULL) ? user : "");
		if (n == 0)
			return 0;
		off += n;
	}
	if ((flags & 0x40) != 0) {
		n = put_len_str(&buf[off], cap - off, pass);
		if (n == 0)
			return 0;
		off += n;
	}

	return off;
}

size_t mqtt_encode_subscribe(uint8_t *buf, size_t cap,
                             uint16_t msg_id,
                             const char *topic, uint8_t qos)
{
	size_t hlen, off, n;
	size_t tlen;

	if (buf == NULL || topic == NULL)
		return 0;
	tlen = str_len_capped(topic, 0xFFFF);
	if (tlen == 0)
		return 0;

	size_t body = 2 + 2 + tlen + 1;
	hlen = put_fixed_header(buf, cap, MQTT_TYPE_SUBSCRIBE | 0x02, (uint32_t)body);
	if (hlen == 0 || cap < hlen + body)
		return 0;
	off = hlen;

	buf[off++] = (uint8_t)(msg_id >> 8);
	buf[off++] = (uint8_t)(msg_id & 0xFF);
	n = put_len_str(&buf[off], cap - off, topic);
	if (n == 0)
		return 0;
	off += n;
	buf[off++] = (uint8_t)(qos & 0x03);

	return off;
}

size_t mqtt_encode_publish(uint8_t *buf, size_t cap,
                           const char *topic,
                           const uint8_t *payload, size_t plen)
{
	size_t hlen, off, n;
	size_t tlen;

	if (buf == NULL || topic == NULL)
		return 0;
	if (payload == NULL && plen > 0)
		return 0;
	tlen = str_len_capped(topic, 0xFFFF);
	if (tlen == 0)
		return 0;

	size_t body = 2 + tlen + plen;
	/* QoS0 / 不保留 —— 巴法云的状态上报不需要 QoS1 确认 */
	hlen = put_fixed_header(buf, cap, MQTT_TYPE_PUBLISH, (uint32_t)body);
	if (hlen == 0 || cap < hlen + body)
		return 0;
	off = hlen;

	n = put_len_str(&buf[off], cap - off, topic);
	if (n == 0)
		return 0;
	off += n;

	for (size_t i = 0; i < plen; i++)
		buf[off++] = payload[i];

	return off;
}

size_t mqtt_encode_pingreq(uint8_t *buf, size_t cap)
{
	if (buf == NULL || cap < 2)
		return 0;
	buf[0] = MQTT_TYPE_PINGREQ;
	buf[1] = 0x00;
	return 2;
}

size_t mqtt_encode_disconnect(uint8_t *buf, size_t cap)
{
	if (buf == NULL || cap < 2)
		return 0;
	buf[0] = MQTT_TYPE_DISCONNECT;
	buf[1] = 0x00;
	return 2;
}

/* ==========================================================================
 * 解码
 * ========================================================================== */

void mqtt_parser_init(mqtt_parser_t *p)
{
	if (p == NULL)
		return;
	p->len = 0;
	p->total = 0;
	p->remain_len = 0;
	p->remain_bytes = 0;
	p->state = 0;
	p->msg.type = 0;
	p->msg.flags = 0;
	p->msg.body = NULL;
	p->msg.blen = 0;
}

static void parser_reset(mqtt_parser_t *p)
{
	p->len = 0;
	p->total = 0;
	p->remain_len = 0;
	p->remain_bytes = 0;
	p->state = 0;
}

int mqtt_parser_feed_byte(mqtt_parser_t *p, uint8_t b)
{
	if (p == NULL)
		return -1;

	if (p->len >= MQTT_MAX_PACKET) {
		parser_reset(p);
		return -1;
	}

	p->buf[p->len++] = b;

	switch (p->state) {
	case 0:										/* 固定头第 1 字节 */
		p->msg.type = (uint8_t)(b & 0xF0);
		p->msg.flags = (uint8_t)(b & 0x0F);
		p->remain_len = 0;
		p->remain_bytes = 0;
		p->state = 1;
		return 0;

	case 1:										/* 剩余长度（1~4 字节可变长整数） */
		p->remain_len |= (uint32_t)(b & 0x7F) << (7 * p->remain_bytes);
		p->remain_bytes++;
		if (p->remain_bytes > 4) {				/* 协议上限 4 字节 */
			parser_reset(p);
			return -1;
		}
		if ((b & 0x80) == 0) {
			if (1u + p->remain_bytes + p->remain_len > MQTT_MAX_PACKET) {
				parser_reset(p);
				return -1;					/* 超出缓冲，丢弃 */
			}
			p->total = (uint16_t)(1 + p->remain_bytes + p->remain_len);
			/* 剩余长度为 0 时（PINGREQ/DISCONNECT）本字节即报文末字节 */
			if (p->remain_len == 0 && p->len >= p->total)
				goto done;
			p->state = 2;
			return 0;
		}
		return 0;

	case 2:										/* body */
		if (p->len >= p->total)
			goto done;
		return 0;

	default:
		parser_reset(p);
		return -1;
	}

done:
	p->msg.body = &p->buf[1 + p->remain_bytes];
	p->msg.blen = (size_t)p->remain_len;
	parser_reset(p);
	return 1;
}

int mqtt_connack_accepted(const mqtt_msg_t *m)
{
	return (m != NULL && m->type == MQTT_TYPE_CONNACK &&
	        m->blen >= 2 && m->body[1] == 0x00);
}

int mqtt_connack_code(const mqtt_msg_t *m)
{
	if (m == NULL || m->type != MQTT_TYPE_CONNACK || m->blen < 2)
		return -1;
	return m->body[1];
}

int mqtt_publish_split(const mqtt_msg_t *m,
                       char *topic, size_t topic_cap,
                       const uint8_t **payload, size_t *plen)
{
	size_t tlen, off;

	if (m == NULL || m->type != MQTT_TYPE_PUBLISH || m->blen < 2)
		return -1;

	tlen = (size_t)((m->body[0] << 8) | m->body[1]);
	off = 2;
	if (off + tlen > m->blen)
		return -1;
	if (topic != NULL) {
		if (tlen + 1 > topic_cap)
			return -2;
		for (size_t i = 0; i < tlen; i++)
			topic[i] = (char)m->body[off + i];
		topic[tlen] = '\0';
	}
	off += tlen;

	/* QoS1 的 PUBLISH 在主题后还有 2 字节报文标识符 */
	if (((m->flags & 0x06) >> 1) == 1) {
		if (off + 2 > m->blen)
			return -1;
		off += 2;
	}

	if (payload != NULL)
		*payload = &m->body[off];
	if (plen != NULL)
		*plen = m->blen - off;
	return 0;
}
