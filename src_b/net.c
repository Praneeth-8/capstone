#include "net.h"

#include<errno.h>
#include<sys/socket.h>
#include<sys/types.h>
#include<unistd.h>

int send_all(int fd,const void *buf, size_t n){

	const char *p = buf;
	size_t sent=0;
	
	while(sent<n){

		ssize_t r = send(fd, p+sent, n-sent, MSG_NOSIGNAL);
		if (r<0){
			if(errno == EINTR){
				continue;
			}
			return -1;
		}
		if(r==0){
			return -1;

		}
		sent+= (size_t)r;
	}
	return 0;

}



int recv_all(int fd,void *buf, size_t n){
	char *p = buf;
	size_t byte_recv = 0;

	while(byte_recv<n){
		ssize_t r = recv(fd, p+byte_recv, n-byte_recv,0);
		if(r<0){
			if(errno == EINTR){
				continue;
			}
			return -1;
		}
		if(r==0){
			return -1;
		}
		byte_recv += (size_t)r;

	}
	return 0;

}
