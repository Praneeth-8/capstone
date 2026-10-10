#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc,char const* argv[]){

	int server_fd , new_socket;
	ssize_t valread;
	struct sockaddr_in address;
	int PORT = (int) *argv[1];
	int opt =1;
	socklen_t addrlen = sizeof(address);
	char buffer[1024] ={0};

	char* hello ="Hello from server ;>";

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
	n = read(new_socket, buffer, sizeof(buffer) - 1);

	FILE* fptr = fopen(buffer,"a");
 while ((n = read(new_socket, buffer, sizeof(buffer) - 1)) > 0) {
    buffer[n] = '\0';
		fputs(buffer,fptr);//write the entire buffer
		fputc('\n',fptr);//write newline char
   }
    // closing the connected socket
    close(new_socket);
  
    // closing the listening socket
    close(server_fd);
    return 0;

}
