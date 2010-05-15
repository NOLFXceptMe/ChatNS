/*
 MIT License/X11 License

 Copyright (c) 2009 Naveen Kumar Molleti, Sweta Yamini S

 Permission is hereby granted, free of charge, to any person
 obtaining a copy of this software and associated documentation
 files (the "Software"), to deal in the Software without
 restriction, including without limitation the rights to use,
 copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the
 Software is furnished to do so, subject to the following
 conditions:

 The above copyright notice and this permission notice shall be
 included in all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 OTHER DEALINGS IN THE SOFTWARE.
*/

#include<stdio.h>
#include<stdlib.h>
#include<time.h>
#include<unistd.h>

#include<pthread.h>
#include<string.h>

#include<sys/select.h>
#include<sys/socket.h>
#include<arpa/inet.h>

#define	SVR_PORT	50000
#define MAX_STR_SZ	500

int main()
{
	int sockfd;
	struct sockaddr_in servAddr;
	char userName[MAX_STR_SZ];
	char messageInputBuffer[MAX_STR_SZ], messageNetworkBuffer[MAX_STR_SZ];
	fd_set fdset;
	int messageInputLength, recvLength;

	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(SVR_PORT);
	servAddr.sin_addr.s_addr = inet_addr("127.0.0.1");

	if((sockfd = socket(AF_INET, SOCK_STREAM, 0))<0){
		printf("Error: Unable to open socket\n");
		exit(-1);
	}
	
	//Name string
	printf("Enter user name: ");
	scanf("%s", userName);
	getchar();
	messageInputLength = strlen(userName)+1;
	
	if(connect(sockfd, (struct sockaddr *)&servAddr, sizeof(servAddr))<0){
		printf("Error: Connecting to server failed\n");
		exit(-1);
	}

	if(send(sockfd, &messageInputLength, sizeof(int), 0)<0){
		printf("Error: Sending user name to server failed\n");
		exit(-1);
	}

	if(send(sockfd, userName, messageInputLength, 0)<0){
		printf("Error: Sending user name to server failed\n");
		exit(-1);
	}

	//Multiplex input on stdin and sockfd
	while(1){
		FD_ZERO(&fdset);
		FD_SET(STDIN_FILENO, &fdset);
		FD_SET(sockfd, &fdset);

		select(sockfd+1, &fdset, NULL, NULL, NULL);

		if(FD_ISSET(STDIN_FILENO, &fdset)){
			memset(messageInputBuffer, '\0', MAX_STR_SZ);
			scanf("%[^\n]s", messageInputBuffer);
			getchar();
			strcat(messageInputBuffer, "\n");
			messageInputLength = strlen(messageInputBuffer);
		
#ifndef NDEBUG
			printf("Client: Sending to server message of length %d, exact string \"%s\"\n", messageInputLength, messageInputBuffer);
#endif	/* NDEBUG */
			if(send(sockfd, &messageInputLength, sizeof(int), 0)<0){
				printf("Error: Sending message to server failed\n");
				close(sockfd);
				exit(-1);
			}
			if(send(sockfd, messageInputBuffer, messageInputLength, 0)<0){
				printf("Error: Sending message to server failed\n");
				exit(-1);
			}
		}else if(FD_ISSET(sockfd, &fdset)){
			memset(messageNetworkBuffer, '\0', MAX_STR_SZ);
			if((recvLength = recv(sockfd, messageNetworkBuffer, MAX_STR_SZ, 0))<0){
				printf("Error: Receiving data from server failed. Shutting down client\n");
				close(sockfd);
				exit(-1);
			}

			if(recvLength == 0){
				printf("Server terminated connection. Shutting down client\n");
				close(sockfd);
				exit(-1);
			}

			printf("%s", messageNetworkBuffer);
		}else{
			printf("Server terminated connection. Shutting down client\n");
			close(sockfd);
			exit(-1);
		}
	}

	return 0;
}
