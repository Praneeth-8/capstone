#include "protocol.h"
#include<inttypes.h>
#include<stdio.h>

int main(){
	struct header valid_header={1,32,1000000};
	struct header invalid_header={7,32,100000};//invalid op
	struct header valid_decoded;
	struct header invalid_decoded;

	uint8_t valbuf[HEADER_SIZE];
	uint8_t invalbuf[HEADER_SIZE];

	encode_header(&valid_header,valbuf);
	encode_header(&invalid_header,invalbuf);

	printf("VALID ENCODING\n");
	for(int i=0;i<HEADER_SIZE;i++){
		if(i<HEADER_SIZE-1){
		printf("%"PRIx8" ",valbuf[i]);}
		else{	printf("%"PRIx8"\n",valbuf[i]);

		}
	}
	printf("INVALID ENCODING\n");
	for(int i=0;i<HEADER_SIZE;i++){
		if(i<HEADER_SIZE-1){
		printf("%"PRIx8" ",invalbuf[i]);}
		else{	printf("%"PRIx8"\n",invalbuf[i]);
		}
	}
	int v1 = decode_header(valbuf,&valid_decoded);
	int inv1 = decode_header(invalbuf,&invalid_decoded);

	printf("Valid Decode:\n");
	printf("OP: %"PRIu8"\n",valid_decoded.op);
	printf("Name_len: %"PRIu16"\n",valid_decoded.name_len);
	printf("File_size: %"PRIu64"\n",valid_decoded.file_size);
	printf("Valid_decode returned: %d\n",v1);

	printf("Invalid Decode: \n");
	printf("OP: %"PRIu8"\n",invalid_decoded.op);
	printf("Name_len: %"PRIu16"\n",invalid_decoded.name_len);
	printf("File_size: %"PRIu64"\n",invalid_decoded.file_size);
	printf("Valid_decode returned: %d\n",inv1);

return 0;
}
