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

#define MAX(num1, num2)	((num1)<(num2)?(num2):(num1))
#define	SVR_PORT	50000
#define MAX_STR_SZ	500

#define MAX_USERS	5
#define ERR_LONG_STR	"Server:: Message too long\n"
#define ERR_USER	"Server:: Too many users, please try after some time\n"
#define ERR_TIMEOUT	"Server:: Online for too long\n"
#define ANNOUNCE_ENTRY	" has entered the chat room\n"

#define TIMEOUT_VAL	600

int sockFd, newSockFd[MAX_USERS];
struct sockaddr_in servAddr, cliAddr[MAX_USERS];
socklen_t servLen, cliLen[MAX_USERS];
pthread_t serverHandlerThread, clientHandlerThread[MAX_USERS], auxThread;
int threadIndex[MAX_USERS];
int auxReadPipes[MAX_USERS][2];		//Aux thread reads from this
int auxWritePipes[MAX_USERS][2];	//Aux thread writes into this

void* serverMain(void *);
void* clientHandlerThreadFunc(void *);
void* auxFunc(void *);
int getNextFreeIndex(void);
int maxAuxReadPipes(int [][2], int);

int main()
{
	pthread_create(&serverHandlerThread, NULL, serverMain, NULL);
	pthread_create(&auxThread, NULL, auxFunc, NULL);

	pthread_join(serverHandlerThread, NULL);
	pthread_join(auxThread, NULL);

	return 0;
}

void* serverMain(void *arg)
{
	int i;
	int nextFreeIndex;
	int tempNewSockFd;
	struct sockaddr_in tempCliAddr;
	socklen_t tempCliLen;
	char errorBuffer[MAX_STR_SZ];

	if((sockFd = socket(AF_INET, SOCK_STREAM, 0))<0){
		printf("Error: Unable to open server socket\n");
		exit(-1);
	}

	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(SVR_PORT);
	servAddr.sin_addr.s_addr = INADDR_ANY;

	if(bind(sockFd, (struct sockaddr *)&servAddr, sizeof(servAddr))<0){
		printf("Error: Unable to bind to port %d\n", SVR_PORT);
		exit(-1);
	}

	if(listen(sockFd, 5)<0){
		printf("Error: listen() call failed. I'm deaf!\n");
		exit(-1);
	}

	//Initialize newSockFd
	for(i=0;i<MAX_USERS;++i)
		newSockFd[i] = -1;

	//Wait on accept call
	while(1){
		tempCliLen = sizeof(tempCliAddr);
		if((tempNewSockFd = accept(sockFd, (struct sockaddr *)&tempCliAddr, &tempCliLen))<0){
			printf("Error: accept() call failed. I'm busy!\n");
			exit(-1);
		}

		nextFreeIndex = getNextFreeIndex();
		if(nextFreeIndex == -1){
			//Send an error message
			memset(errorBuffer, 0, MAX_STR_SZ);
			strcpy(errorBuffer, ERR_USER);

			if(send(tempNewSockFd, errorBuffer, strlen(errorBuffer), 0)<0){
				printf("Error: Sending error message failed\n");
			}
			shutdown(tempNewSockFd, SHUT_RDWR);
			close(tempNewSockFd);
		}else{
			newSockFd[nextFreeIndex] = tempNewSockFd;
			cliAddr[nextFreeIndex] = tempCliAddr;
			cliLen[nextFreeIndex] = tempCliLen;
			threadIndex[nextFreeIndex] = nextFreeIndex;
			printf("Info: Connection accepted from %s as client %d\n", inet_ntoa(tempCliAddr.sin_addr), nextFreeIndex);

			pthread_create(&clientHandlerThread[nextFreeIndex], NULL, clientHandlerThreadFunc, &threadIndex[nextFreeIndex]);
		}
		
	}
	
	return NULL;
}

void* clientHandlerThreadFunc(void* index)
{
	int threadIndex = *((int *)index);
	char userName[MAX_STR_SZ];
	int messageLength, recvLength, metaBufferLength;
	char *messageBuffer = NULL;
	fd_set fdset;
	struct timeval timeout;
	char errorBuffer[MAX_STR_SZ];
	char metaBuffer[2*MAX_STR_SZ];
	time_t currentTime;
	char currentTimeString[MAX_STR_SZ];
	
	timeout.tv_sec = TIMEOUT_VAL;
	timeout.tv_usec = 0;
	
	//Receive user name
	if((recvLength = recv(newSockFd[threadIndex], &messageLength, sizeof(int), 0))<0){
		printf("Error: recv() failed on client %d, terminating connection\n", threadIndex);
		close(newSockFd[threadIndex]);
		newSockFd[threadIndex] = -1;
		return NULL;
	}
	if((recvLength = recv(newSockFd[threadIndex], userName, messageLength, 0))<0){
		printf("Error: recv() failed on client %d, terminating connection\n", threadIndex);
		close(newSockFd[threadIndex]);
		newSockFd[threadIndex] = -1;
		return NULL;
	}

	//Announce arrival
	memset(metaBuffer, 0, 2*MAX_STR_SZ);
	memset(currentTimeString, 0, MAX_STR_SZ);
	currentTime = time(NULL);
	strcpy(currentTimeString, ctime(&currentTime));
	strcat(metaBuffer, "[");
	strncat(metaBuffer, currentTimeString, strlen(currentTimeString)-1);
	strcat(metaBuffer, "]");
	strcat(metaBuffer, userName);
	strcat(metaBuffer, ANNOUNCE_ENTRY);
	metaBufferLength = strlen(metaBuffer);

	write(auxReadPipes[threadIndex][1], &metaBufferLength, sizeof(int));
	write(auxReadPipes[threadIndex][1], metaBuffer, metaBufferLength);
	if(messageBuffer != NULL)
		free(messageBuffer);

	while(1){
		FD_ZERO(&fdset);
		FD_SET(auxWritePipes[threadIndex][0], &fdset);
		FD_SET(newSockFd[threadIndex], &fdset);

		select(MAX(auxWritePipes[threadIndex][0], newSockFd[threadIndex])+1, &fdset, NULL, NULL, &timeout);

		if(FD_ISSET(auxWritePipes[threadIndex][0], &fdset)){
			recvLength = read(auxWritePipes[threadIndex][0], &messageLength, sizeof(int));
			if(recvLength<sizeof(int)){
				printf("Error: Expected %u of data, failed\n", sizeof(int));
				break;
			}

			messageBuffer = (char *)malloc((messageLength+1)*sizeof(char));
			memset(messageBuffer, '\0', messageLength+1);
			recvLength = read(auxWritePipes[threadIndex][0], messageBuffer, messageLength);
			if(recvLength<messageLength){
				printf("Error: Expected %d bytes of data, received %d bytes of data\n", messageLength, recvLength);
				break;
			}

			//Send message to the client	
			if(send(newSockFd[threadIndex], messageBuffer, messageLength, 0)<0){
				printf("Error: Sending message failed to client\n");
				break;
			}
			free(messageBuffer);
		}else if(FD_ISSET(newSockFd[threadIndex], &fdset)){
			if((recvLength = recv(newSockFd[threadIndex], &messageLength, sizeof(int), 0))<0){
				printf("Error: Receiving data from client failed\n");
				break;
			}

			messageBuffer = (char *)malloc((messageLength+1)*sizeof(char));
			memset(messageBuffer, '\0', messageLength+1);
			if((recvLength = recv(newSockFd[threadIndex], messageBuffer, messageLength, 0))<0){
				printf("Error: Receiving data from client failed\n");
				break;
			}
			if(recvLength == 0){
				printf("Info: Client %d terminated connection\n", threadIndex);
				break;
			}

			if(recvLength>MAX_STR_SZ){
				//Send an error message
				memset(errorBuffer, '\0', MAX_STR_SZ);
				strcpy(errorBuffer, ERR_LONG_STR);

				if(send(newSockFd[threadIndex], errorBuffer, strlen(errorBuffer), 0)<0){
					printf("Error: Sending error message failed\n");
					break;
				}
				free(messageBuffer);
				continue;
			}

#ifndef NDEBUG
			fprintf(stderr, "Thread %d: Message received from client of length %d, expected length %d, exact string:\"%s\"", threadIndex, recvLength, messageLength, messageBuffer);
#endif	/* NDEBUG */
			//Append timestamp and nicks
			memset(metaBuffer, '\0', 2*MAX_STR_SZ);
			memset(currentTimeString, '\0', MAX_STR_SZ);
			currentTime = time(NULL);
			strcpy(currentTimeString, ctime(&currentTime));
			
			strcat(metaBuffer, "[");
			strncat(metaBuffer, currentTimeString, strlen(currentTimeString)-1);
			strcat(metaBuffer, "]<");
			strcat(metaBuffer, userName);
			strcat(metaBuffer, ">");
			strcat(metaBuffer, messageBuffer);
			metaBufferLength = strlen(metaBuffer);
	
#ifndef NDEBUG
			printf("Thread %d:: To auxThread, sending %d bytes, exact string \"%s\"\n", threadIndex, metaBufferLength, metaBuffer);		
#endif	/* NDEBUG */
			//Send data to aux thread
			write(auxReadPipes[threadIndex][1], &metaBufferLength, sizeof(int));
			write(auxReadPipes[threadIndex][1], metaBuffer, metaBufferLength);
			free(messageBuffer);
		}else{
			memset(errorBuffer, 0, MAX_STR_SZ);
			strcpy(errorBuffer, ERR_TIMEOUT);

			memset(metaBuffer, '\0', 2*MAX_STR_SZ);
			memset(currentTimeString, '\0', MAX_STR_SZ);
			currentTime = time(NULL);
			strcpy(currentTimeString, ctime(&currentTime));
			
			strcat(metaBuffer, "[");
			strncat(metaBuffer, currentTimeString, strlen(currentTimeString)-1);
			strcat(metaBuffer, "]");
			strcat(metaBuffer, errorBuffer);
			metaBufferLength = strlen(metaBuffer);
	
			if(send(newSockFd[threadIndex], metaBuffer, metaBufferLength, 0)<0){
				printf("Error: Sending error message failed\n");
			}
			break;
		}
	}

	close(newSockFd[threadIndex]);
	newSockFd[threadIndex] = -1;

	return NULL;
}

void *auxFunc(void *arg)
{
	int i;
	int activePipeIndex;
	fd_set fdset;
	int messageLength;
	char* messageBuffer;
	
	for(i=0;i<MAX_USERS;++i){
		pipe(auxReadPipes[i]);
		pipe(auxWritePipes[i]);
	}

	while(1){
		FD_ZERO(&fdset);
		for(i=0;i<MAX_USERS;++i)
			FD_SET(auxReadPipes[i][0], &fdset);

		select(maxAuxReadPipes(auxReadPipes, 0)+1, &fdset, NULL, NULL, NULL);
		for(i=0;i<MAX_USERS;++i){
			if(FD_ISSET(auxReadPipes[i][0], &fdset)){
				activePipeIndex = i;
				continue;
			}
		}

		read(auxReadPipes[activePipeIndex][0], &messageLength, sizeof(int));
		messageBuffer = (char *)malloc((messageLength+1)*sizeof(char));
		memset(messageBuffer, 0, messageLength+1);
		read(auxReadPipes[activePipeIndex][0], messageBuffer, messageLength);
#ifndef NDEBUG
		printf("auxThread:: Received from thread %d exact string \"%s\" of length %d\n", activePipeIndex, messageBuffer, messageLength);
#endif	/* NDEBUG */

		for(i=0;i<MAX_USERS;++i){
			if(newSockFd[i] != -1){
#ifndef NDEBUG
				printf("auxThread:: Writing message to pipe %d\n", i);
#endif	/* NDEBUG */
				write(auxWritePipes[i][1], &messageLength, sizeof(int));
				write(auxWritePipes[i][1], messageBuffer, messageLength);
			}
		}
	}
}

int getNextFreeIndex()
{
	int i;
	for(i=0;i<MAX_USERS;++i){
		if(newSockFd[i] == -1)
			return i;
	}
	return -1;
}

int maxAuxReadPipes(int array[][2], int index)
{
	int i;
	int max = array[0][index];

	for(i=1;i<MAX_USERS;++i){
		if(array[i][index]>max)
			max = array[i][index];
	}

	return max;
}
