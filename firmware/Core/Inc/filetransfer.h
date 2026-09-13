#ifndef FILETRANSFER_H
#define FILETRANSFER_H
#include "stm32f1xx_hal.h"
#include <stdint.h>

#define PACKET_NUMBER 8192
#define PACKET_ADDRESS_SIZE 4
#define PACKET_DATA_SIZE 128
#define PACKET_CRC32_SIZE 4
#define PACKET_SIZE (PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE + PACKET_CRC32_SIZE)
#define PACKET_MAX_DELAY 2000 // Maybe too generous

#define STATUS_GOOD 0
#define STATUS_BAD  1

#define SIZE_MAGIC_NB  4
#define SIZE_TYPE      2
#define SIZE_ID        2
#define SIZE_HEADER    (SIZE_MAGIC_NB + SIZE_TYPE + SIZE_ID)
#define SIZE_FOOTER    4
#define SIZE_ED_STATUS 2
#define SIZE_ED_DATA   (PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE)

#define TYPE_ACK     0
#define TYPE_ERASE   1
#define TYPE_SEND    2
#define TYPE_RECEIVE 3
#define TYPE_TEST    4
#define TYPE_DATA    0xA

uint64_t xorshift64(uint32_t nb);
uint8_t send_packet(uint8_t *buf, uint16_t size, uint8_t free_buf);
uint8_t receive_packet(uint16_t size, uint16_t *type, uint8_t *extra_data, uint32_t ms_delay);
uint8_t receive_data_packet(uint32_t *address, uint8_t data[PACKET_DATA_SIZE]);
uint8_t *create_packet(uint16_t type, uint8_t *extra_data, uint16_t ed_size, uint16_t id);
uint8_t *create_data_packet(uint32_t address, uint8_t *data);

void receive_file(void (*program_byte)(int, uint16_t));
void send_file(uint16_t (*read_byte)(int));

#endif
