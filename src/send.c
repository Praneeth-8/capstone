#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "net.h"

int Send_file (FILE* file,const char ip[],const char file_name[],int PORT)
{

    int client_fd = socket(AF_INET, SOCK_STREAM, 0);


    struct sockaddr_in serv_addr = {0};

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

   inet_pton(AF_INET, ip, &serv_addr.sin_addr);
   int connection = connect(client_fd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); //establish connection
	 
	char* line = NULL;
	size_t len = 0;
	ssize_t read;

	//send file_name 
	send_all(client_fd,file_name,strlen(file_name));
	
	while((read = getline(&line,&len,file))!=-1){ // read the file line by line 
		send_all(client_fd, line, read);
  }

  close(client_fd);
  free(line);
  return 0;
}

int main(int argc,char* argv[]){
	//run as ./send recv_ip[1] file_name[2]
	FILE* fptr = fopen(argv[3],"r");
	Send_file(fptr,argv[1],argv[3],atoi(argv[2]));
	fclose(fptr);
}
