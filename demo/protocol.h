#ifndef PROTOCOL_H
#define PROTOCOL_H
#include <stdint.h>
#define HEADER_SIZE 11
#define MAX_NAME_LEN 4096 //Linux allows paths up to 4096

enum Op {
	OP_SEND_FILE = 1,
	OP_DELETE,
	OP_ACK,
	OP_ERROR,
};

struct header {
	uint8_t op;
	uint16_t name_len;
	uint64_t file_size;

};

void encode_header(const struct header *h,	uint8_t H[]);

int decode_header(const uint8_t *buf, struct header *h);


#endif
