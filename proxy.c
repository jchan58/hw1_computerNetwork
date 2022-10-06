#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netdb.h>
#include <netinet/in.h>
#include <errno.h>
#include <netinet/tcp.h>

/* TODO: proxy()
 * Establish a socket connection to listen for incoming connections.
 * Accept each client request in a new process.
 * Parse header of request and get requested URL.
 * Get data from requested remote server.
 * Send data to the client
 * Return 0 on success, non-zero on failure
*/

// function used to parse the data 

int parse_data(int child_descriptor){
  int data_read; 
  char buffer[100000];
  char buf[100000];
  int length = 0; 

  data_read = read(child_descriptor, buf, 99999);

  if(data_read < 0){
    perror("Error: Couldn't read from the buffer");
  }

  length = strlen(buf);

  //parse the request 

  //create a struct first to pass in parameters 
  struct ParsedRequest*  request = ParsedRequest_create();
  if (ParsedRequest_parse(request, buffer, length) < 0) {
  sprintf(buffer,"HTTP/1.0 400 Bad Request\r\n\r\n");
  int error_length = write(child_descriptor, buffer, strlen(buffer));
  if(error_length < 0){
    perror("Error: Couldn't write to the file descriptor");
    return -1;
  }

  // send the request to server by acting as the client 
  int client_socket; 
  int port;
  struct sockaddr_in server_address;
  struct hostent *server_host; 

  //get the port number from the struct
  if(request->port == NULL) {
    port = 80;
  } else {
    port = atoi(request->port);
  }

  //create the socket 
  client_socket = socket(AF_INET,SOCK_STREAM,0);
  if(client_socket < 0) {
    perror("Couldn't open socket in child proc");
  }

  server_address.sin_port = htons(port); 
  server_address.sin_family = AF_INET;
  server_address.sin_addr.s_addr = server_host->h_addr;

  //connect to the server
  int connection = connect(client_socket, (struct sockaddr *) &server_address, sizeof(server_address));

  //check connection
  if (connection == -1) {
    perror("Error: Cannot connect");
    return 1;
  }

  //edit the request to the server 
  data_read = ParsedHeader_set(request,"Host",request->host);
  if(data_read < 0) {
    perror("Couldn't parse");
  }

  data_read = ParsedHeader_set(request, "Connection", "close");
  if(data_read < 0) {
    perror("Parse set couldn't close connection");
  }

  //add the null terminator to the result that needs to be sent to the client 
  //get the length of the request
  int len = ParsedRequest_totalLen(request);
  char* terminate_buf = (char*) malloc (len + 1);
  if(ParsedRequest_unparse(request, terminate_buf, len) < 0){
    perror("Error: Couldn't unparse the header");
  }

  terminate_buf[len] = '\0';

  //send request to server
  int data_sent;

  data_sent = write(client_socket, terminate_buf, len);

  if(data_sent < 0) {
    perror("Error: Couldn't write to the server");
  }

  //read the result from the server and send it to the client
  while(1) {
    memset(&buffer,0,100000);
    data_read = read(client_socket, buffer, 99999);
    if(data_read < 0) {
      perror("Error: Couldn't read from server");
    }
    if(data_read == 0) {
      break;
    }

    data_read = write(child_descriptor, buffer, strlen(buffer));
    if(data_read < 0) {
      perror("Error: Couldn't write to client");
    }
  }
  }
  return 0; 
}


int proxy(char *proxy_port) {
  //create a socket 
  int proxy_socket; 
  proxy_socket = socket(AF_INET, SOCK_STREAM, 0);

  //check the connection of the server socket 

  if(proxy_socket == -1) {
    perror("Error: Connection error");
    return 1; 
  }

   //specify and address for a socket
  int port = atoi(proxy_port);
  struct sockaddr_in proxy_address;
  proxy_address.sin_family = AF_INET;
  proxy_address.sin_port = htons(port); 
  proxy_address.sin_addr.s_addr = INADDR_ANY;

  //bind the server 
  int bind_descriptor = bind(proxy_socket, (struct sockaddr *) &proxy_address, sizeof(proxy_address));

  //check the connection 
  if(bind_descriptor == -1) {
    perror("Error: Connection error");
    return 1; 
  }

   //listen for incoming connections
  int listen_descriptor = listen(proxy_socket, 100);
  int processes = 0; 
  int fork_value = 0; 

  //This is the proxy acting as the server

  while (processes <= 100) {
    struct sockaddr_in client_address;
    socklen_t length = sizeof(client_address);
    int accept_descriptor = accept(proxy_socket, (struct sockaddr *) &client_address, &length);
    
    //check the connection - if estabilished if not try the next client
    if(accept_descriptor < 0){
      perror("Couldn't accept incoming connection");
    } else {
      // the connection has been accepted fork a new process for the new client 
      fork_value = fork(); 

      if(fork_value < 0) {
        perror("Error: Could not create a process");
        return 1;
      }
      // we are in the child process 
      if(fork_value == 0) {
        close(proxy_socket);
        int process_id = getpid();
        processes++;
        //parse the request from the client once recieved 
        int result = parse_data(accept_descriptor);
      } else {
        // we are in the parent process
        close(accept_descriptor);
      }
    }
  }

  return 0;
}


int main(int argc, char * argv[]) {
  char *proxy_port;

  if (argc != 2) {
    fprintf(stderr, "Usage: ./proxy <port>\n");
    exit(EXIT_FAILURE);
  }

  proxy_port = argv[1];
  return proxy(proxy_port);
}