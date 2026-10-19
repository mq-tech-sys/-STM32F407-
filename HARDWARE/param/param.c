/**
  ******************************************************************************
  * @文件     param.c
  * @摘要     掉电保存实现：密码 + 卡片列表，写在内部 Flash 的 S3 扇区
  *
  *   为什么不直接"改一次就擦一次扇区"：
  *     16KB 扇区擦除要约 250ms，而且擦写寿命只有 1 万次左右。
  *     这里改成**日志式追加**：每次修改往扇区尾部写一条 64 字节记录，
  *     上电时扫描全部记录、取每条类型里序号最大的那条。
  *     256 个槽位，写满才整体擦一次，寿命和速度都够用。
  *
  *   记录格式（共 64 字节，CRC 覆盖前 60 字节）：
  *     magic(4) seq(4) type(1) len(1) data(50) crc16(2) rsv(2)
  *
  *   断电安全：写入是"整条覆盖"，一条记录要么完整要么被 CRC 判为无效，
  *   上一条有效记录仍然可用，不会出现"密码变成半截"的情况。
  *
  *   测试：定义 PARAM_HOST_TEST 后 Flash 操作改为读写一块内存数组，
  *   可以在 PC 上跑 tests/test_param.c，不需要板子。
  ******************************************************************************
**/

#include "include.h"
#include "stm32f4xx_flash.h"
#include "param.h"

/* --------------------------------------------------------------------------
 * 存储布局
 * -------------------------------------------------------------------------- */
#define PARAM_SECTOR_ADDR   0x0800C000u          /* S3，紧接 48KB 代码区之后 */
#define PARAM_SECTOR        FLASH_Sector_3
#define PARAM_SECTOR_SIZE   0x4000u              /* 16KB */
#define PARAM_REC_SIZE      64u
#define PARAM_REC_COUNT     (PARAM_SECTOR_SIZE / PARAM_REC_SIZE)   /* 256 条 */
#define PARAM_REC_DATA      50u
#define PARAM_CRC_BYTES     60u                  /* magic..data 的字节数 */

#define PARAM_MAGIC         0x504D5241u          /* 'PARM' */
#define PARAM_TYPE_PWD      1u
#define PARAM_TYPE_CARD     2u

#define PARAM_CARD_MAX      10u

#ifdef PARAM_HOST_TEST
/* PC 单元测试：用一块内存冒充 Flash */
extern uint8_t g_param_flash[PARAM_SECTOR_SIZE];
#define PARAM_BASE          ((uintptr_t)g_param_flash)
#define PARAM_LOG(...)      do { } while (0)
#else
#define PARAM_BASE          ((uintptr_t)PARAM_SECTOR_ADDR)
#define PARAM_LOG(...)      printf(__VA_ARGS__)
#endif

typedef struct
{
    uint32_t magic;
    uint32_t seq;
    uint8_t  type;
    uint8_t  len;
    uint8_t  data[PARAM_REC_DATA];
    uint16_t crc;
    uint16_t rsv;
} param_rec_t;

/* 结构体必须正好 64 字节，否则记录会跨槽位 */
typedef char param_rec_size_must_be_64[(sizeof(param_rec_t) == PARAM_REC_SIZE) ? 1 : -1];

static SemaphoreHandle_t s_mutex = NULL;
static uint32_t          s_seq   = 0;
static uint8_t           s_buf[PARAM_REC_SIZE];

/* --------------------------------------------------------------------------
 * 基础工具
 * -------------------------------------------------------------------------- */

/* CRC16-CCITT（poly 0x1021，不反射），和 tools/ota_pack.py 里的算法一致 */
static uint16_t param_crc16(const uint8_t *p, uint32_t n)
{
    uint16_t crc = 0xFFFF;
    uint32_t i;
    uint8_t  b;

    for (i = 0; i < n; i++)
    {
        crc ^= (uint16_t)((uint16_t)p[i] << 8);
        for (b = 0; b < 8; b++)
        {
            if (crc & 0x8000)
                crc = (uint16_t)((crc << 1) ^ 0x1021);
            else
                crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static void param_read(uint16_t slot, param_rec_t *r)
{
    const void *addr = (const void *)(PARAM_BASE + (uintptr_t)slot * PARAM_REC_SIZE);
    memcpy(r, addr, sizeof(param_rec_t));
}

static uint8_t param_erase_all(void)
{
#ifdef PARAM_HOST_TEST
    memset((void *)PARAM_BASE, 0xFF, PARAM_SECTOR_SIZE);
    return 1;
#else
    FLASH_Status st;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
    st = FLASH_EraseSector(PARAM_SECTOR, VoltageRange_3);
    FLASH_Lock();

    return (st == FLASH_COMPLETE) ? 1 : 0;
#endif
}

static uint8_t param_program(uintptr_t addr, const uint8_t *data, uint32_t len)
{
#ifdef PARAM_HOST_TEST
    memcpy((void *)addr, data, len);
    return 1;
#else
    uint32_t i;
    uint8_t  ok = 1;

    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
                    FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);

    for (i = 0; i + 3 < len; i += 4)
    {
        uint32_t w = (uint32_t)data[i]
                   | ((uint32_t)data[i + 1] << 8)
                   | ((uint32_t)data[i + 2] << 16)
                   | ((uint32_t)data[i + 3] << 24);

        if (FLASH_ProgramWord((uint32_t)addr + i, w) != FLASH_COMPLETE)
        {
            ok = 0;
            break;
        }
    }

    FLASH_Lock();
    return ok;
#endif
}

/* --------------------------------------------------------------------------
 * 记录读写
 * -------------------------------------------------------------------------- */

static uint8_t param_write(uint16_t slot, uint8_t type, const uint8_t *data, uint8_t len)
{
    param_rec_t *r = (param_rec_t *)s_buf;

    if (len > PARAM_REC_DATA || slot >= PARAM_REC_COUNT)
        return 0;

    memset(s_buf, 0xFF, PARAM_REC_SIZE);
    r->magic = PARAM_MAGIC;
    r->seq   = ++s_seq;
    r->type  = type;
    r->len   = len;
    if (len > 0 && data != NULL)
        memcpy(r->data, data, len);
    r->crc = param_crc16(s_buf, PARAM_CRC_BYTES);
    r->rsv = 0xFFFF;

    return param_program(PARAM_BASE + (uintptr_t)slot * PARAM_REC_SIZE,
                         s_buf, PARAM_REC_SIZE);
}

/* 扇区写满：擦掉，把当前 RAM 里的完整状态重写一遍 */
static uint8_t param_compact(void)
{
    uint8_t pwd[PASSWORD_MAX_LEN];
    uint8_t cards[1 + PARAM_CARD_MAX * 4];
    uint8_t plen, clen, n, i;

    plen = (uint8_t)strlen(g_password);
    if (plen > PASSWORD_MAX_LEN)
        plen = PASSWORD_MAX_LEN;
    memcpy(pwd, g_password, plen);

    n = g_card_count;
    if (n > PARAM_CARD_MAX)
        n = PARAM_CARD_MAX;
    cards[0] = n;
    for (i = 0; i < n; i++)
        memcpy(&cards[1 + i * 4], g_card_list[i].uid, 4);
    clen = (uint8_t)(1 + n * 4);

    PARAM_LOG("[PARAM] 记录写满，擦除并压缩...\r\n");

    if (!param_erase_all())
        return 0;

    s_seq = 0;
    if (!param_write(0, PARAM_TYPE_PWD, pwd, plen))
        return 0;
    if (!param_write(1, PARAM_TYPE_CARD, cards, clen))
        return 0;

    return 1;
}

static uint8_t param_append(uint8_t type, const uint8_t *data, uint8_t len)
{
    param_rec_t r;
    uint16_t slot;

    for (slot = 0; slot < PARAM_REC_COUNT; slot++)
    {
        param_read(slot, &r);
        if (r.magic == 0xFFFFFFFFu)          /* 擦除态：这个槽还没写过 */
            return param_write(slot, type, data, len);
    }

    if (!param_compact())
        return 0;

    /* 压缩后用掉了 0、1 号槽，新记录写 2 号 */
    return param_write(2, type, data, len);
}

static uint8_t param_append_locked(uint8_t type, const uint8_t *data, uint8_t len)
{
    uint8_t ok;

    if (s_mutex != NULL)
        xSemaphoreTake(s_mutex, portMAX_DELAY);

    ok = param_append(type, data, len);

    if (s_mutex != NULL)
        xSemaphoreGive(s_mutex);

    return ok;
}

/* --------------------------------------------------------------------------
 * 对外接口
 * -------------------------------------------------------------------------- */

void param_init(void)
{
    param_rec_t r;
    uint16_t    i;
    uint16_t    pwd_slot = 0, card_slot = 0;
    uint32_t    pwd_seq = 0, card_seq = 0;
    uint8_t     have_pwd = 0, have_card = 0;

    if (s_mutex == NULL)
        s_mutex = xSemaphoreCreateMutex();

    s_seq = 0;

    for (i = 0; i < PARAM_REC_COUNT; i++)
    {
        param_read(i, &r);

        if (r.magic != PARAM_MAGIC)
            continue;
        if (r.crc != param_crc16((const uint8_t *)&r, PARAM_CRC_BYTES))
            continue;
        if (r.len > PARAM_REC_DATA)
            continue;

        if (r.seq > s_seq)
            s_seq = r.seq;

        if (r.type == PARAM_TYPE_PWD && r.len >= 1 && r.len <= PASSWORD_MAX_LEN)
        {
            if (!have_pwd || r.seq > pwd_seq)
            {
                have_pwd = 1;
                pwd_seq  = r.seq;
                pwd_slot = i;
            }
        }
        else if (r.type == PARAM_TYPE_CARD && r.len >= 1)
        {
            if (!have_card || r.seq > card_seq)
            {
                have_card = 1;
                card_seq  = r.seq;
                card_slot = i;
            }
        }
    }

    if (have_pwd)
    {
        uint8_t n;

        param_read(pwd_slot, &r);
        n = r.len;
        if (n > PASSWORD_MAX_LEN)
            n = PASSWORD_MAX_LEN;
        memcpy(g_password, r.data, n);
        g_password[n] = '\0';
    }

    if (have_card)
    {
        uint8_t n, k;

        param_read(card_slot, &r);
        n = r.data[0];
        if (n > PARAM_CARD_MAX)
            n = PARAM_CARD_MAX;
        if ((uint16_t)(1 + n * 4) > r.len)
            n = 0;                            /* 记录长度对不上，整条丢弃 */

        memset(g_card_list, 0, sizeof(g_card_list));
        for (k = 0; k < n; k++)
            memcpy(g_card_list[k].uid, &r.data[1 + k * 4], 4);
        g_card_count = n;
    }

    PARAM_LOG("[PARAM] 加载完成：密码%s，卡片 %u 张\r\n",
              have_pwd ? "已恢复" : "用默认", (unsigned)g_card_count);
}

uint8_t param_save_password(void)
{
    uint8_t len = (uint8_t)strlen(g_password);

    if (len > PASSWORD_MAX_LEN)
        len = PASSWORD_MAX_LEN;
    if (len == 0)
        return 0;               /* empty password is never persisted */

    return param_append_locked(PARAM_TYPE_PWD, (const uint8_t *)g_password, len);
}

uint8_t param_save_cards(void)
{
    uint8_t buf[1 + PARAM_CARD_MAX * 4];
    uint8_t n, i;

    n = g_card_count;
    if (n > PARAM_CARD_MAX)
        n = PARAM_CARD_MAX;

    buf[0] = n;
    for (i = 0; i < n; i++)
        memcpy(&buf[1 + i * 4], g_card_list[i].uid, 4);

    return param_append_locked(PARAM_TYPE_CARD, buf, (uint8_t)(1 + n * 4));
}
