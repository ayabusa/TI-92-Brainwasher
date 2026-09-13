#include "debug.h"
#include <stdint.h>

// returns 0 if exact length, 1 if it was truncated or timeout
// if ms_timeout is 0, wait infinitly
int debug_get_buf(uint8_t *buf, uint32_t len, uint32_t ms_timeout) {
    int start_tick = HAL_GetTick();
    while (RX_BUF_PERSO_LEN < len)
        if (HAL_GetTick() - start_tick > ms_timeout && ms_timeout)
            break;
    if (len != RX_BUF_PERSO_LEN) {
        RX_BUF_PERSO_LEN = 0;
        return 1;
    }
    memcpy(buf, (const void *)RX_BUF_PERSO, len);
    RX_BUF_PERSO_LEN = 0;
    return 0;
}

void debug_print(const char *s) {
    uint16_t len = strlen(s);
    uint32_t t0 = HAL_GetTick();
    while (1) {
        uint8_t r = CDC_Transmit_FS((uint8_t *)s, len);
        if (r == USBD_OK) return;
        if (HAL_GetTick() - t0 > 100) return;
    }
}

void debug_printn(const uint8_t *s, uint16_t n) {
    uint32_t t0 = HAL_GetTick();
    while (1) {
        uint8_t r = CDC_Transmit_FS((uint8_t *)s, n);
        if (r == USBD_OK) return;
        if (HAL_GetTick() - t0 > 100) return;
    }
}

char *debug_get_str(void) {
    while (RX_BUF_PERSO_LEN == 0) { };
    char *res = calloc(1, RX_BUF_PERSO_LEN+1);
    memcpy(res, (const void *)RX_BUF_PERSO, RX_BUF_PERSO_LEN);
    RX_BUF_PERSO_LEN = 0;
    return res;
}

char debug_get_char(void) {
    char *s = debug_get_str();
    char c = s[0];
    free(s);
    return c;
}

void  debug_reset_buf(void) {
    RX_BUF_PERSO_LEN = 0;
}
