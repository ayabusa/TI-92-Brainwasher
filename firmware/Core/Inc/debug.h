#ifndef DEBUG_H
#define DEBUG_H

#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "usb_device.h"
#include "usbd_cdc_if.h"

extern volatile uint8_t RX_BUF_PERSO[];
extern volatile uint32_t RX_BUF_PERSO_LEN;

void  debug_print (const char *s);
void  debug_printn(const uint8_t *s, uint16_t n);
int   debug_get_buf(uint8_t *buf, uint32_t len, uint32_t ms_timeout);
char *debug_get_str(void);
char  debug_get_char(void);
void  debug_reset_buf(void);

#endif
