#ifndef TFTP_COMMON_H
#define TFTP_COMMON_H

#include <stdint.h>
#include "../utils/tftp_utils.h"

typedef struct {
	uint16_t opcode;
	union {
		struct {
			uint16_t block_n;
			char data[TFTP_DATA_SIZE];
		} data_pkt;
		
		struct {
			uint16_t block_n;
		} ack_pkt;
		
		struct {
			uint16_t error_code;
			char error_msg[100];
		} error_pkt;
	};
} tftp_packet_t;

#endif
