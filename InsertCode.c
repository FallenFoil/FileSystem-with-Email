#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>

#define PORT 25555

int main(int argc, char const *argv[]){
	int sock = 0;

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0){
		printf("\n Socket creation error \n");
		return -1;
	}

	struct sockaddr_in serv_addr;
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(PORT);

	// Convert IPv4 and IPv6 addresses from text to binary form
	if(inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr)<=0){
		printf("\nInvalid address/ Address not supported \n");
		return -1;
	}

	if(connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0){
		printf("\nConnection Failed \n");
		return -1;
	}

	pid_t pid = fork();
	if(pid){
		char err[1];
		read(sock, err, 1);
		if(err[0] == '1' || err[0] == '2'){
			printf("Times Up\n");
			kill(pid, SIGKILL);
		}

		if(err[0] == '0'){
			printf("Code received with success\n");
		}

		close(sock);
	}
	else{
		printf("Insert access code:\n");

		char buff[37];
		read(0, buff, 37);
		buff[36] = '\0';
		printf("Sending code\n");
		ssize_t sended = send(sock, buff, 37, 0);

		if(sended > 0){
			printf("Code sended with success\n");
		}
	}

	return 0;
}
