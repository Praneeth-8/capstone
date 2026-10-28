#include <netinet/in.h>
#include <stdio.h>
#include<stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include  "net.h"

int main(int argc,char const* argv[]){

		int server_fd , new_socket;
		ssize_t valread;
		struct sockaddr_in address;
		int PORT = atoi(argv[1]);
		int opt =1;
		socklen_t addrlen = sizeof(address);
		char buffer[1024] ={0};


	//creating fd for socket
		server_fd = socket(AF_INET, SOCK_STREAM, 0);

	//forcefully attach socket to the port 8080
		setsockopt(server_fd, SOL_SOCKET,SO_REUSEADDR | SO_REUSEPORT, &opt,sizeof(opt));

		address.sin_family = AF_INET;
		address.sin_addr.s_addr = INADDR_ANY;
		address.sin_port = htons(PORT);

		int bind_val = bind(server_fd, (struct sockaddr*)&address,sizeof(address));

		int listen_val = listen(server_fd,3);

		new_socket = accept(server_fd, (struct sockaddr*)&address,&addrlen);
  
    // subtract 1 for the null
    // terminator at the end
		
		int n;
		char filename[30]={0};
		
		n = recv_all(new_socket, filename, sizeof(buffer) - 1);
		FILE* fptr = fopen(filename,"a");
	
	while ((n = recv_all(new_socket, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = '\0';
		
		fputs(buffer,fptr);//write the entire buffer
		
		fputc('\n',fptr);//write newline char
  }
	//close file	
	fclose(fptr);
    // closing the connected socket
    
	close(new_socket);
  
    // closing the listening socket
    close(server_fd);
    return 0;

}
