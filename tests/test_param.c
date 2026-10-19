/**
  ******************************************************************************
  * @file     tests/test_param.c
  * @brief    Host-side unit test for the parameter persistence module.
  *
  *   Compile param.c with -DPARAM_HOST_TEST so flash access goes to the
  *   g_param_flash[] array. Everything else (record scan, CRC check, append,
  *   compaction) runs for real, so this covers the logic without a board.
  *
  *   Build / run:
  *     gcc -std=c99 -Wall -Wextra -DPARAM_HOST_TEST -I tests/stub -I HARDWARE/param \
  *         HARDWARE/param/param.c tests/test_param.c -o tests/test_param.exe
  *     tests/test_param.exe
  *
  *   Output is ASCII only on purpose: run_tests.bat pipes it through a GBK
  *   console, mixed encodings break the log.
  ******************************************************************************
**/

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "include.h"

/* Stand-in for the 16KB flash sector */
uint8_t g_param_flash[0x4000];

/* Globals owned by param.c */
char     g_password[PASSWORD_MAX_LEN + 1] = "123456";
CARD_UID g_card_list[10];
uint8_t  g_card_count = 0;

/* Minimal FreeRTOS stubs (single threaded test, the mutex does nothing) */
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (SemaphoreHandle_t)(uintptr_t)1; }
int xSemaphoreTake(SemaphoreHandle_t s, TickType_t w) { (void)s; (void)w; return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }

static int g_fail = 0;

static void check(int cond, const char *msg)
{
    if (cond)
    {
        printf("  ok    %s\n", msg);
    }
    else
    {
        printf("  FAIL  %s\n", msg);
        g_fail++;
    }
}

/* Erase the "flash" and reload, i.e. first boot on a blank part */
static void fresh_boot(void)
{
    memset(g_param_flash, 0xFF, sizeof(g_param_flash));
    strcpy(g_password, "123456");
    memset(g_card_list, 0, sizeof(g_card_list));
    g_card_count = 0;
    param_init();
}

/* Power cycle: RAM resets to defaults, only flash survives */
static void reboot(void)
{
    strcpy(g_password, "123456");
    memset(g_card_list, 0, sizeof(g_card_list));
    g_card_count = 0;
    param_init();
}

static void add_card(uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    g_card_list[g_card_count].uid[0] = a;
    g_card_list[g_card_count].uid[1] = b;
    g_card_list[g_card_count].uid[2] = c;
    g_card_list[g_card_count].uid[3] = d;
    g_card_count++;
}

int main(void)
{
    uint8_t i;

    printf("==============================================\n");
    printf(" param persistence unit test (password / cards)\n");
    printf("==============================================\n");

    /* ---- 1. blank flash keeps the compiled defaults ---- */
    printf("\n[1] blank flash\n");
    fresh_boot();
    check(strcmp(g_password, "123456") == 0, "password stays 123456");
    check(g_card_count == 0, "card count is 0");

    /* ---- 2. password survives a reboot ---- */
    printf("\n[2] password\n");
    strcpy(g_password, "246810");
    check(param_save_password() == 1, "save returns 1");
    reboot();
    check(strcmp(g_password, "246810") == 0, "password is 246810 after reboot");

    /* ---- 3. cards survive a reboot ---- */
    printf("\n[3] cards\n");
    add_card(0x11, 0x22, 0x33, 0x44);
    add_card(0xAA, 0xBB, 0xCC, 0xDD);
    add_card(0x01, 0x02, 0x03, 0x04);
    check(param_save_cards() == 1, "save returns 1");
    reboot();
    check(g_card_count == 3, "3 cards after reboot");
    check(g_card_list[0].uid[0] == 0x11 && g_card_list[0].uid[3] == 0x44, "card 1 uid intact");
    check(g_card_list[2].uid[0] == 0x01 && g_card_list[2].uid[3] == 0x04, "card 3 uid intact");

    /* ---- 4. delete shifts the list, still correct after reboot ---- */
    printf("\n[4] delete card\n");
    for (i = 1; i + 1 < g_card_count; i++)
        memcpy(g_card_list[i].uid, g_card_list[i + 1].uid, 4);
    g_card_count--;
    check(param_save_cards() == 1, "save returns 1");
    reboot();
    check(g_card_count == 2, "2 cards after reboot");
    check(g_card_list[1].uid[0] == 0x01, "second card became the old third");

    /* ---- 5. a torn record falls back to the previous one ---- */
    printf("\n[5] torn record\n");
    fresh_boot();
    strcpy(g_password, "111111");
    param_save_password();
    strcpy(g_password, "222222");
    param_save_password();
    g_param_flash[64 + 12] ^= 0xFF;          /* corrupt the 2nd record */
    reboot();
    check(strcmp(g_password, "111111") == 0, "bad record skipped, 111111 restored");

    /* ---- 6. compaction after the sector fills up ---- */
    printf("\n[6] compaction (256 slots)\n");
    fresh_boot();
    for (i = 0; i < 250; i++)
    {
        sprintf(g_password, "%06u", (unsigned)(100000 + i));
        if (param_save_password() != 1)
        {
            check(0, "write 250 failed");
            break;
        }
    }
    reboot();
    check(strcmp(g_password, "100249") == 0, "latest value 100249 after 250 writes");

    for (i = 0; i < 20; i++)
    {
        sprintf(g_password, "%06u", (unsigned)(200000 + i));
        param_save_password();
    }
    reboot();
    check(strcmp(g_password, "200019") == 0, "latest value 200019 after compaction");

    /* ---- 7. the two record types do not clobber each other ---- */
    printf("\n[7] password and cards together\n");
    fresh_boot();
    strcpy(g_password, "135790");
    param_save_password();
    add_card(1, 2, 3, 4);
    add_card(5, 6, 7, 8);
    param_save_cards();
    reboot();
    check(strcmp(g_password, "135790") == 0, "password still 135790");
    check(g_card_count == 2, "still 2 cards");

    /* ---- 8. empty password is refused ---- */
    printf("\n[8] boundary\n");
    fresh_boot();
    g_password[0] = '\0';                    /* '#' with no digits pressed */
    check(param_save_password() == 0, "empty password rejected");
    reboot();
    check(strcmp(g_password, "123456") == 0, "default password kept");

    strcpy(g_password, "654321");
    check(param_save_password() == 1, "6 digit password saved");
    reboot();
    check(strcmp(g_password, "654321") == 0, "6 digit password restored");

    printf("\n----------------------------------------------\n");
    if (g_fail == 0)
    {
        printf(" all 8 groups passed.\n");
        printf("----------------------------------------------\n");
        return 0;
    }
    printf(" %d check(s) failed.\n", g_fail);
    printf("----------------------------------------------\n");
    return 1;
}
