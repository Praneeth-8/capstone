#include "protocol.h"
#include<string.h>
#include<arpa/inet.h>
#include<endian.h>

void encode_header(const struct header *h,	uint8_t H[]){
	H[0] = h->op;
	uint16_t ord_name_len=htons(h->name_len);
	memcpy(H+1,&ord_name_len,sizeof(uint16_t));
			
	uint64_t ord_file_size	= htobe64(h->file_size);

	memcpy(H+3,&ord_file_size,sizeof(uint64_t));

}
int decode_header(const uint8_t *buf,struct header *h){
	uint16_t name_len_net;
	uint64_t file_size_net;

	memcpy(&name_len_net,buf+1,sizeof name_len_net);
	memcpy(&file_size_net,buf+3,sizeof file_size_net);

	h->op = buf[0];
	h->name_len = ntohs(name_len_net);
	h->file_size = be64toh(file_size_net);

	if(h->op<OP_SEND_FILE || h->op >OP_ERROR){
		return -1;//invalid ops
	}
	if(h->name_len > MAX_NAME_LEN){
		return -1;//name len too big than can handle
	}
	if((h->op==OP_SEND_FILE|| h->op==OP_DELETE)&& h->name_len==0){
		return -1;//valid op , invalid name
	}

return 0;
}
