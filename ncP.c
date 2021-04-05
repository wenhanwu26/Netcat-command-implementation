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
#include <sys/ioctl.h>

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
// Return a listening socket
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
void add_to_pfds(struct pollfd* pfds[], int newfd, int* fd_count, int* fd_size)
{
	// If we don't have room, add more space in the pfds array
	//if (*fd_count == *fd_size) {
	//    *fd_size *= 2; // Double it

	//    *pfds = realloc(*pfds, sizeof(**pfds) * (*fd_size));
	//}


	(*pfds)[*fd_count].fd = newfd;
	(*pfds)[*fd_count].events = POLLIN; // Check ready-to-read

	(*fd_count)++;
}

// Remove an index from the set
void del_from_pfds(struct pollfd pfds[], int i, int* fd_count)
{
	// Copy the one from the end over this one
	pfds[i] = pfds[*fd_count - 1];

	(*fd_count)--;
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

	int fd_size = 3; //if no -r then 3

	if (r) {
		fd_size = 7;// Start off with room for 7 connections (5 connections + self + stdin)
	}
	struct pollfd* pfds = malloc(sizeof * pfds * fd_size);

	listener = get_listener_socket(hostname, port);
	if (listener == -1) {
		fprintf(stderr, "error getting listening socket\n");
		exit(1);
	}

	// Add the listener to set
	pfds[0].fd = listener;
	pfds[0].events = POLLIN; // Report ready to read on incoming connection


	// Add the stdin to set
	pfds[1].fd = 0; //standard input
	pfds[1].events = POLLIN; // Report ready to read on incoming connection

	fd_count = 2; // For the listener

	int disconnected = 0;

	fprintf(stderr, "Waiting for a connection \n");

	while (1) {
		bzero(buf, 1024);
		int poll_count = poll(pfds, fd_count, -1);

		if (poll_count == -1) {
			perror("poll");
			exit(1);
		}

		//printf("fdcount %d\n", fd_count);

		// Run through the existing connections looking for data to read
		for (int i = 0; i < fd_count; i++) {



			// Check if someone's ready to read
			if (pfds[i].revents & POLLIN) { // We got one!!

				if (pfds[i].fd == listener) {
					// If listener is ready to read, handle new connection


					addrlen = sizeof remoteaddr;

					if (fd_count < fd_size) {
						newfd = accept(listener,
							(struct sockaddr*)&remoteaddr,
							&addrlen);
						fprintf(stderr, "Connection accepted \n");

						//int nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);

						if (newfd == -1) {
							perror("accept");
						}
						else {
							add_to_pfds(&pfds, newfd, &fd_count, &fd_size);

							/* fprintf(stderr, "pollserver: new connection from %s on "
								 "socket %d\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN), newfd);*/

							fprintf(stderr, "The IP address %s\n", inet_ntop(remoteaddr.ss_family, get_in_addr((struct sockaddr*)&remoteaddr), remoteIP, INET6_ADDRSTRLEN));

							fprintf(stderr, "The port number %d\n", ntohs(get_in_port((struct sockaddr*)&remoteaddr)));
						}
					}
				}
				else {
					// If not the listener, we're just a regular client or stdin
					int nbytes;

					if (pfds[i].fd == 0) {//standard input
						fgets(buf, sizeof(buf), stdin);
						//buf[strlen(buf) - 1] = '\0';
						nbytes = 1; //not trigger error checking
					}
					else {
						nbytes = recv(pfds[i].fd, buf, sizeof buf, 0);
					}

					int sender_fd = pfds[i].fd;

					if (nbytes <= 0) {
						// Got error or connection closed by client
						if (nbytes == 0) {
							// Connection closed
							printf("pollserver: socket %d hung up\n", sender_fd);
						}
						else {
							perror("recv");
						}

						close(pfds[i].fd); // Bye!

						del_from_pfds(pfds, i, &fd_count);

						if (fd_count <= 2) {
							disconnected = 1;
						}

					}
					else {
						// We got some good data from a client

						for (int j = 0; j < fd_count; j++) {
							// Send to everyone!
							int dest_fd = pfds[j].fd;

							// Except the listener and ourselves and stdin
							if (dest_fd != listener && dest_fd != sender_fd && dest_fd != 0) {
								if (send(dest_fd, buf, strlen(buf), 0) == -1) {
									perror("send");
								}
							}

						}
						if (sender_fd != 0) { //not sent to standard output if get from standard input
							buf[strlen(buf) - 1] = '\0';
							printf("%s\n", buf);
						}
					}
				} // END handle data from client
			} // END got ready-to-read from poll()
		} // END looping through file descriptors

		if (disconnected && k == 0) {
			break;
		}
	}
	close(listener);

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

	char buf[1024];
	char sendbuf[1024];


	int fd_count = 0;
	int fd_size = 2; //stdin and connected server
	struct pollfd* pfds = malloc(sizeof * pfds * fd_size);
	pfds[0].fd = sockfd;
	pfds[0].events = POLLIN; // Report ready to read on server
	pfds[1].fd = 0;//stdin
	pfds[1].events = POLLIN; // ready to read on stdin
	fd_count = 2;

	int disconnected = 0;
	while (1) {
		int poll_count = poll(pfds, fd_count, timeout * 1000);

		if (poll_count == -1) {
			perror("poll");
			exit(1);
		}

		if (poll_count == 0) {
			break;
		}

		bzero(sendbuf, 1024);
		bzero(buf, 1024);
		for (int i = 0; i < fd_count; i++) {
			if (pfds[i].revents & POLLIN) {// We got one!!
				if (pfds[i].fd == 0) {//stdin
					fgets(sendbuf, sizeof(sendbuf), stdin);
					//sendbuf[strlen(sendbuf) - 1] = '\0';

					//send to server
					int bytes_sent = send(sockfd, sendbuf, strlen(sendbuf), 0);

				}
				else {//get data from server
					int read = recv(sockfd, buf, 1024, 0);
					if (read == 0) {
						disconnected = 1;
					}
					else {
						buf[strlen(buf) - 1] = '\0';
						printf("%s\n", buf);
					}
				}

			}
		}
		if (disconnected) {
			break;
		}

	}
	close(sockfd);
	freeaddrinfo(res);
}
int main(int argc, char** argv) {

	// This is some sample code feel free to delete it
	// This is the main program for the thread version of nc
	// test

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
