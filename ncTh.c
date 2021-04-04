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

//host name and port number want ot start a server
void server(char* hostname,unsigned int port) {
    int status;
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints;
    struct addrinfo* res;  // will point to the results
    int sockfd, new_fd;
    char s[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints); // make sure the struct is empty
    hints.ai_family = AF_INET;     // IPv4 
    hints.ai_socktype = SOCK_STREAM; // TCP stream sockets
    hints.ai_flags = AI_PASSIVE;     // fill in my IP for me  //change later, as well as port

    char portNum[12]; 
    sprintf(portNum, "%d", port);
    
    if ((status = getaddrinfo(hostname, portNum, &hints, &res)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }

    // servinfo now points to a linked list of 1 or more struct addrinfos

    // ... do everything until you don't need servinfo anymore ....
    
    // make a socket, bind it, and listen on it:
    sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

    int yes = 1; //to reuse socket
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
        perror("setsockopt");
        exit(1);
    }

    bind(sockfd, res->ai_addr, res->ai_addrlen);
    listen(sockfd, 10);//how many pending connections queue will hold

    char buf[1024]; 
    // now accept an incoming connection:
    while (1) {
        addr_size = sizeof their_addr;
        new_fd = accept(sockfd, (struct sockaddr*)&their_addr, &addr_size);

        if (new_fd < 0) {
            perror("Could not establish new connection\n");
        }
        inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr*)&their_addr),s, sizeof s);
        printf("server: got connection from %s\n", s);

        printf("The port number %d\n", ntohs(get_in_port((struct sockaddr*)&their_addr)));
        //char* msg = "Sever connected!";
        //int len, bytes_sent;

        //len = strlen(msg);
        //bytes_sent = send(new_fd, msg, len, 0);
        

        while (1) {

            bzero(buf, 1024);
            int read = recv(new_fd, buf, 1024, 0);
            if (!read) break; // done reading
            if (read < 0) { perror("cannot read"); }

            printf("Server received: '%s'\n", buf);
            int bytes_sent = send(new_fd, buf, strlen(buf), 0);
        }
        close(new_fd);
    }
    close(sockfd);
    
    freeaddrinfo(res); // free the linked-list
}

//host name and port number want to connect to
void client(char* hostname, unsigned int port, unsigned int sourceport) {//add source port
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
    connect(sockfd, res->ai_addr, res->ai_addrlen); //destination

    //char* msg = "Client say hi";
    //int len, bytes_sent;

    //len = strlen(msg);
    //bytes_sent = send(sockfd, msg, len, 0);
    //printf("%d", bytes_sent);
    
    
    char buf[1024];
    char sendbuf[1024];


    while (1) {

        bzero(sendbuf, 1024);
        
        printf("What you want to send ?\n");
        fgets(sendbuf, sizeof(sendbuf), stdin);
        sendbuf[strlen(sendbuf) - 1] = '\0';
        printf("Going to send: '%s'\n", sendbuf);
        int bytes_sent = send(sockfd, sendbuf, strlen(sendbuf), 0);

        bzero(buf, 1024);
        int read = recv(sockfd, buf, 1024, 0);
        if (!read) break; // done reading
        if (read < 0) { perror("cannot read"); }
        printf("Client received: '%s'\n", buf);

	}
    close(sockfd);
    freeaddrinfo(res);
}
int main(int argc, char **argv) {
  
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
      server(cmdOps.hostname, cmdOps.port);
  }
  client(cmdOps.hostname, cmdOps.port, cmdOps.source_port);
  
}
