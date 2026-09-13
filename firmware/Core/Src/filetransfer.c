#include "filetransfer.h"
#include "crc32.h"
#include "debug.h"
#include <string.h>
#include <stdint.h>
#include <stdio.h>

// Really basic random nb generator, code from wikipedia
// https://en.wikipedia.org/wiki/Xorshift
uint64_t xorshift64(uint32_t nb) {
	uint64_t x = nb;
	x ^= x << 7;
	x ^= x >> 9;
	return x;
}

// returns 0 when good, 1 when no response
uint8_t send_packet(uint8_t *buf, uint16_t size, uint8_t free_buf) {
    uint16_t status = STATUS_BAD;
    uint8_t ack[SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER] = {0};
    while (status != STATUS_GOOD) {
        debug_printn(buf, size);
        if (debug_get_buf(ack, SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER, PACKET_MAX_DELAY)) {
            if (free_buf)
                free(buf);
            return 1;
        }
        status = *(uint16_t *)(ack + SIZE_HEADER);
    }
    if (free_buf)
        free(buf);
    return 0;
}

// returns 0 when good, 1 when bad
// if ms_delay is 0 wait forever
uint8_t receive_packet(uint16_t size, uint16_t *type, uint8_t *extra_data, uint32_t ms_delay) {
    uint8_t *packet = malloc(size);
    while (1) {
        if (debug_get_buf(packet, size, ms_delay)) {
            free(packet);
            return 1;
        }
        uint32_t received_checksum = *(uint32_t *)(packet + size - SIZE_FOOTER);
        uint32_t computed_checksum = crc32(packet, size - SIZE_FOOTER);
        uint16_t id = *(uint16_t *)(packet + SIZE_MAGIC_NB + SIZE_TYPE);
        if (received_checksum == computed_checksum) {
            // create ACK packet and send it with GOOD status
            uint16_t status_data = STATUS_GOOD;
            uint8_t *ack = create_packet(TYPE_ACK, (uint8_t *)(&status_data), SIZE_ED_STATUS, id);
            debug_printn(ack, SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER);
            free(ack);

            *type = *(uint16_t *)(packet + SIZE_MAGIC_NB);
            if (extra_data)
                memcpy(extra_data, packet + SIZE_HEADER, size - SIZE_HEADER - SIZE_FOOTER);
            free(packet);
            return 0;
        }
        // We loop again because CRC is bad
        // create ACK packet and send it with BAD status
        uint16_t status_data = STATUS_BAD;
        uint8_t *ack = create_packet(TYPE_ACK, (uint8_t *)(&status_data), SIZE_ED_STATUS, id);
        debug_printn(ack, SIZE_HEADER + SIZE_ED_STATUS + SIZE_FOOTER);
        free(ack);
    }
}

// 0 good, 1 failed
uint8_t receive_data_packet(uint32_t *address, uint8_t data[PACKET_DATA_SIZE]) {
    uint16_t type;
    uint8_t extra_data[SIZE_ED_DATA];
    if (receive_packet(SIZE_HEADER + SIZE_ED_DATA + SIZE_FOOTER, &type, extra_data, PACKET_MAX_DELAY))
        return 1;
    *address = *(uint32_t *)(extra_data);
    memcpy(data, extra_data + PACKET_ADDRESS_SIZE, PACKET_DATA_SIZE);
    return 0;
}

// extra_data can be NULL (ed_size should be 0),
// and id can be 0 (it will then be randomly generated)
uint8_t *create_packet(uint16_t type, uint8_t *extra_data, uint16_t ed_size, uint16_t id) {
    uint8_t *packet = calloc(1, SIZE_HEADER + ed_size + SIZE_FOOTER);
    *(uint32_t *)(packet) = 0x13374267; // Magic nb
    *(uint16_t *)(packet + SIZE_MAGIC_NB) = type;
    if (!id) // Pseudo-random id
        id = (uint16_t)xorshift64(HAL_GetTick());
    *(uint16_t *)(packet + SIZE_MAGIC_NB + SIZE_TYPE) = id;
    if (extra_data && ed_size) // Add data if there is some
        memcpy(packet + SIZE_HEADER, extra_data, ed_size);
    *(uint32_t *)(packet + SIZE_HEADER + ed_size) = crc32(packet, SIZE_HEADER + ed_size); // The packet checksum
    return packet;
}

uint8_t *create_data_packet(uint32_t address, uint8_t *data) {
    uint8_t *extra_data = malloc(SIZE_ED_DATA);
    *(uint32_t *)(extra_data) = address;
    memcpy(extra_data + PACKET_ADDRESS_SIZE, data, PACKET_DATA_SIZE);
    uint8_t *res = create_packet(TYPE_DATA, extra_data, SIZE_ED_DATA, 0);
    free(extra_data);
    return res;
}

void receive_file(void (*program_byte)(int, uint16_t)) {
    int i = 0;
    uint8_t packet[PACKET_SIZE] = {0};
    while (i < PACKET_NUMBER) {
        // Get da data
        if (debug_get_buf(packet, PACKET_SIZE, PACKET_MAX_DELAY)) {
            debug_printn((uint8_t *)"B", 1); // We send Bad
            continue;
        }
        // Check CRC32
        uint32_t computed_checksum = crc32(packet, PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE);
        uint32_t received_checksum;
        memcpy(&received_checksum, packet + PACKET_DATA_SIZE + PACKET_ADDRESS_SIZE, sizeof(uint32_t));
        if (computed_checksum != received_checksum) {
            debug_printn((uint8_t *)"B", 1); // We send Bad
            continue;
        }
        // We retrieve the address to write the buffer to
        uint32_t address = 0;
        memcpy(&address, packet, 4);

        // Write da data
        for (int k=0; k<PACKET_DATA_SIZE/2; k++) {
            program_byte(address + k, ((uint16_t *)(packet + PACKET_ADDRESS_SIZE ))[k]);
            __NOP(); __NOP(); __NOP();
        }

        debug_printn((uint8_t *)"G", 1); // We send Good
        i++;
    }
}

void send_file(uint16_t (*read_byte)(int)) {
    int i = 0;
    uint8_t packet[PACKET_SIZE] = {0};
    while (i < PACKET_NUMBER) {
        uint32_t address = (PACKET_DATA_SIZE/2) * i;
        ((uint32_t*)packet)[0] = address;
        for (int k = 0; k<PACKET_DATA_SIZE/2; k++)
            ((uint16_t *)packet)[PACKET_ADDRESS_SIZE/2 + k] = read_byte(address + k);
        uint32_t checksum = crc32(packet, PACKET_ADDRESS_SIZE + PACKET_DATA_SIZE);
        memcpy(packet + PACKET_DATA_SIZE + PACKET_ADDRESS_SIZE, &checksum, sizeof(uint32_t));
        // We send da packet
        debug_printn(packet, PACKET_SIZE);
        // We check the response
        uint8_t resp = debug_get_char();
        if (resp != 'G')
            continue; // This is bad so we resend the message
        i++;
    }
}
