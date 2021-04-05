#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include "commonProto.h"
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>
#include <netdb.h>
#include <poll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

//https://beej.us/guide/bgnet/html/#poll
// get sockaddr, IPv4 or IPv6:
void* get_in_addr(struct sockaddr* sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}

	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}
//https://stackoverflow.com/questions/2371910/how-to-get-the-port-number-from-struct-addrinfo-in-unix-c
in_port_t get_in_port(struct sockaddr* sa)
{
	if (sa->sa_family == AF_INET)
		return (((struct sockaddr_in*)sa)->sin_port);

	return (((struct sockaddr_in6*)sa)->sin6_port);
}
int get_listener_socket(char* hostname, unsigned int port)
{
	int listener;     // Listening socket descriptor
	int yes = 1;        // For setsockopt() SO_REUSEADDR, below
	int rv;

	struct addrinfo hints, * ai, * p;

	// Get us a socket and bind it
	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // IPv4 
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
	hints.ai_flags = AI_PASSIVE;     // fill in my IP for me  

	char portNum[12];
	sprintf(portNum, "%d", port);

	if ((rv = getaddrinfo(hostname, portNum, &hints, &ai)) != 0) {
		fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(rv));
		exit(1);
	}

	for (p = ai; p != NULL; p = p->ai_next) {
		listener = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
		if (listener < 0) {
			continue;
		}

		// Lose the pesky "address already in use" error message
		setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));

		if (bind(listener, p->ai_addr, p->ai_addrlen) < 0) {
			close(listener);
			continue;
		}

		break;
	}

	freeaddrinfo(ai); // All done with this

	// If we got here, it means we didn't get bound
	if (p == NULL) {
		return -1;
	}

	// Listen
	if (listen(listener, 10) == -1) {
		return -1;
	}

	return listener;
}
// Add a new file descriptor to the set

pthread_mutex_t mutex1 = PTHREAD_MUTEX_INITIALIZER; //fd_count is a shared resource should be locked by mutex

void add_to_fds(int* fds[], int newfd, int* fd_count, int* fd_size)
{
	pthread_mutex_lock(&mutex1);
	(*fds)[*fd_count] = newfd;
	//(*fds)[*fd_count].events = POLLIN; // Check ready-to-read

	(*fd_count)++;
	pthread_mutex_unlock(&mutex1);
}

// Remove an index from the set
void del_from_fds(int fds[], int i, int* fd_count)
{
	// Copy the one from the end over this one
	pthread_mutex_lock(&mutex1); 
	fds[i] = fds[*fd_count - 1];

	(*fd_count)--;
	pthread_mutex_unlock(&mutex1);
}

struct serverInfo {
	int* fds;   // indicates if -k was provided.
	int* fd_count;
	int fd_size;
};
// https://blog.gtwang.org/programming/pthread-multithreading-programming-in-c-tutorial/
void* child(void* data) {
	struct sockaddr_storage remoteaddr;
	socklen_t addrlen;
	int listener, newfd;
	char remoteIP[INET6_ADDRSTRLEN];
	char buf[1024];
	//struct serverInfo* info = (serverInfo*)data;
	struct serverInfo info = *(struct serverInfo*)data;

	int* fds = info.fds;
	int* fd_count = info.fd_count;
	int fd_size = info.fd_size;
	listener = fds[0];


	while (1) {
		addrlen = sizeof remoteaddr;
		newfd = accept(listener,
			(struct sockaddr*)&remoteaddr,
			&addrlen);
		fprintf(stderr, "Connection accepted \n");

		if (newfd == -1) {
			perror("accept");
		}
		else {
			add_to_fds(&fds, newfd, fd_count, &fd_size);
			/* fprintf(stderr, "pollserver: new connection from %s on "
				 "socket %d\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN), newfd);*/

			fprintf(stderr, "The IP address %s\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN));

			fprintf(stderr, "The port number %d\n", ntohs(get_in_port((struct sockaddr*)&remoteaddr)));
		}

		int nbytes;
		int i;//index of current fd in the table
		int sender_fd;
		do {

			bzero(buf, 1024);
			nbytes = recv(newfd, buf, sizeof buf, 0);

			buf[strlen(buf) - 1] = '\0';
			printf("%s\n", buf);

			sender_fd = newfd;
			//send to other fd

			for (int j = 0; j < *fd_count; j++) {
				// Send to everyone!
				int dest_fd = fds[j];

				// Except the listener and ourselves and stdin
				if (dest_fd == sender_fd) {
					i = j;
				}
				if (dest_fd != listener && dest_fd != sender_fd && dest_fd != 0) {
					if (send(dest_fd, buf, strlen(buf), 0) == -1) {
						perror("send");
					}
				}
			}
		} while (nbytes > 0);

		// Got error or connection closed by client
		if (nbytes == 0) {
			// Connection closed
			printf("pollserver: socket %d hung up\n", sender_fd);
		}
		else {
			perror("recv");
		}

		close(newfd); // Bye!

		del_from_fds(fds, i, fd_count);

	}
	pthread_exit(NULL);
}

void* readstdin(void* data) {
	int listener;
	char buf[1024];
	//struct serverInfo* info = (serverInfo*)data;
	struct serverInfo info = *(struct serverInfo*)data;

	int* fds = info.fds;
	int* fd_count = info.fd_count;
	int fd_size = info.fd_size;
	listener = fds[0];
	while (1) {
		int nbytes;
		int sender_fd;
		bzero(buf, 1024);
		//nbytes = recv(0, buf, sizeof buf, 0); //0 stdin

		fgets(buf, sizeof(buf), stdin);


		//do not print to stdoutput
		//buf[strlen(buf)-1] = '\0';
		//printf("%s\n", buf);

		sender_fd = 0;
		//send to other fd

		for (int j = 0; j < *fd_count; j++) {
			// Send to everyone!
			int dest_fd = fds[j];

			// Except the listener and ourselves and stdin

			if (dest_fd != listener && dest_fd != sender_fd && dest_fd != 0) {
				if (send(dest_fd, buf, strlen(buf), 0) == -1) {
					perror("send");
				}
			}
		}
	}
	pthread_exit(NULL);
}
//host name and port number want ot start a server
void server(char* hostname, unsigned int port, int k, int r) {
	int status;
	struct sockaddr_storage remoteaddr;
	socklen_t addrlen;
	int listener, newfd;
	char remoteIP[INET6_ADDRSTRLEN];
	char buf[1024];

	int fd_count = 0;

	int fd_size = 3; //if no -r then 3 (1 connection + self +stdin)

	if (r) {
		fd_size = 7;// Start off with room for 7 connections (5 connections + self + stdin)
	}


	listener = get_listener_socket(hostname, port);
	if (listener == -1) {
		fprintf(stderr, "error getting listening socket\n");
		exit(1);
	}


	int* fds = malloc(sizeof * fds * fd_size);

	fds[0] = listener;

	fds[1] = 0; //standard input

	fd_count = 2; // For the listener + stdin

	int disconnected = 0;

	fprintf(stderr, "Waiting for a connection \n");

	//create four thread
	struct serverInfo info = { fds,&fd_count,fd_size };

	pthread_t t;
	pthread_create(&t, NULL, readstdin, &info);//create child

	if (r) {
		pthread_t t1;
		pthread_create(&t1, NULL, child, &info);//create child

		pthread_t t2;
		pthread_create(&t2, NULL, child, &info);//create child

		pthread_t t3;
		pthread_create(&t3, NULL, child, &info);//create child

		pthread_t t4;
		pthread_create(&t4, NULL, child, &info);//create child

		pthread_t t5;
		pthread_create(&t5, NULL, child, &info);//create child




	}
	else {
		pthread_t t1;
		pthread_create(&t1, NULL, child, &info);//create child

	}


	int hasConnected = 0;

	while (1) {
		if (fd_count > 2) {
			hasConnected = 1;
		}else if (hasConnected && fd_count <= 2 && k == 0) {
			break;
		}
	}

	close(listener);

}


pthread_mutex_t mutex2 = PTHREAD_MUTEX_INITIALIZER;
int isTimeout(time_t start, int duration)
{
	time_t stop;
	stop = time(NULL);
	if (stop - start > duration && duration > 0) {
		return 1;
	}
	else {
		return 0;
	}
}

void updateTime(time_t* start) {
	pthread_mutex_lock(&mutex2);
	*start = time(NULL);
	pthread_mutex_unlock(&mutex2);
}

struct clientInfo {
	int fd;   // indicates if -k was provided.
	time_t* start;
	int* disconnected;
};

//for thread running
void* waitServer(void* data) {
	int listener;
	char buf[1024];
	//struct serverInfo* info = (serverInfo*)data;
	struct clientInfo info = *(struct clientInfo*)data;

	listener = info.fd;
	time_t* start = info.start;
	int* disconnected = info.disconnected;

	while (1) {
		int nbytes;
		int sender_fd;
		bzero(buf, 1024);
		int read = recv(listener, buf, 1024, 0);

		if (read == 0) {
			*disconnected = 1;
			
		}
		else {

			updateTime(start);

			buf[strlen(buf) - 1] = '\0';
			printf("%s\n", buf);
		}

		if (*disconnected == 1) {
			pthread_exit(NULL);
		}

	}
	pthread_exit(NULL);

}

//for thread running
void* clientStdin(void* data) {
	int listener;
	char buf[1024];
	//struct serverInfo* info = (serverInfo*)data;
	struct clientInfo info = *(struct clientInfo*)data;

	listener = info.fd;

	int* disconnected = info.disconnected;

	time_t* start = info.start;
	while (1) {
		bzero(buf, 1024);
		fgets(buf, sizeof(buf), stdin);
		updateTime(start);

		int bytes_sent = send(listener, buf, strlen(buf), 0);

		if (*disconnected == 1) {
			pthread_exit(NULL);
		}

	}
	pthread_exit(NULL);

}
//host name and port number want to connect to
void client(char* hostname, unsigned int port, unsigned int sourceport, unsigned int timeout) {//add source port
	int status;
	struct addrinfo hints;
	struct addrinfo* res;  // will point to the results
	int sockfd;

	memset(&hints, 0, sizeof hints); // make sure the struct is empty
	hints.ai_family = AF_INET;     // IPv4 
	hints.ai_socktype = SOCK_STREAM; // TCP stream sockets


	//convert int to string
	char portNum[12];
	sprintf(portNum, "%d", port);
	// get ready to connect
	status = getaddrinfo(hostname, portNum, &hints, &res);

	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);


	// get ready to connect


	//info require to bind socket
	if (sourceport != 0) {
		char sourceportNum[12];
		sprintf(sourceportNum, "%d", sourceport);
		struct addrinfo hintsClient, * resClient;
		memset(&hintsClient, 0, sizeof hintsClient);
		hintsClient.ai_family = AF_INET;  // use IPv4 
		hintsClient.ai_socktype = SOCK_STREAM;
		hintsClient.ai_flags = AI_PASSIVE;     // fill in my IP for me
		int ai = getaddrinfo(NULL, sourceportNum, &hintsClient, &resClient);
		//printf("get successfully? '%d'\n", ai);



		int yes = 1; //to reuse socket
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
			perror("setsockopt");
			exit(1);
		}

		int success = bind(sockfd, resClient->ai_addr, resClient->ai_addrlen); //bind client address (source port)
		//printf("Bind successfully? '%d'\n", success);
	}

	if (connect(sockfd, res->ai_addr, res->ai_addrlen) == -1) {
		fprintf(stderr, "Connection fail\n");
		return;
	}

	freeaddrinfo(res);

	time_t start = time(NULL);

	int disconnected = 0;


	struct clientInfo info = { sockfd,&start,&disconnected };

	pthread_t t;
	pthread_create(&t, NULL, clientStdin, &info);

	pthread_t t1;
	pthread_create(&t1, NULL, waitServer, &info);

	while (1) {

		if (isTimeout(start, timeout)|| disconnected ) {
			break;
		}
	}

	close(sockfd);
	
}
int main(int argc, char** argv) {

	// This is some sample code feel free to delete it
	// This is the main program for the thread version of nc

	struct commandOptions cmdOps;
	int retVal = parseOptions(argc, argv, &cmdOps);
	printf("Command parse outcome %d\n", retVal);

	printf("-k = %d\n", cmdOps.option_k);
	printf("-l = %d\n", cmdOps.option_l);
	printf("-v = %d\n", cmdOps.option_v);
	printf("-r = %d\n", cmdOps.option_r);
	printf("-p = %d\n", cmdOps.option_p);
	printf("-p port = %u\n", cmdOps.source_port);
	printf("-w  = %d\n", cmdOps.option_w);
	printf("Timeout value = %u\n", cmdOps.timeout);
	printf("Host to connect to = %s\n", cmdOps.hostname);
	printf("Port to connect to = %u\n", cmdOps.port);

	//if l then server, else client
	if (cmdOps.option_l) {
		server(cmdOps.hostname, cmdOps.port, cmdOps.option_k, cmdOps.option_r);
	}
	else {
		if (cmdOps.option_w == 1) {
			client(cmdOps.hostname, cmdOps.port, cmdOps.source_port, cmdOps.timeout);
		}
		else {
			client(cmdOps.hostname, cmdOps.port, cmdOps.source_port, -1);
		}
	}
}