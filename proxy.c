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

#define MAX_CLIENTS 100
int socket_descriptor;
pid_t client_PID[MAX_CLIENTS];

/* TODO: proxy()
 * Establish a socket connection to listen for incoming connections.
 * Accept each client request in a new process.
 * Parse header of request and get requested URL.
 * Get data from requested remote server.
 * Send data to the client
 * Return 0 on success, non-zero on failure
*/

// function used to parse the data 


//function used to check the version of the Https request 

int connectServer(char* host_name, int port){

  int current_socket = socket(AF_INET, SOCK_STREAM, 0);
  if(current_socket < 0) {
    perror("Error: Couldn't create socket");
    return -1;
  } 

  //get the host name 
  struct hostent *host = gethostbyname(host_name);
  if(host == NULL) {
    perror("Error: Host doesn't exist");
    return -1;
  }

  struct sockaddr_in server_address;
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);

  if(connect(current_socket, (struct sockaddr*)&server_address, (socklen_t)sizeof(server_address)) < 0 )
	{
    perror("Error: Couldn't connect");
    return -1;
  }

return current_socket;

}

int handleRequest(int clientSocket, struct ParsedRequest *request, char *client_buffer){

  strcpy(client_buffer, "GET ");
  strcat(client_buffer, request->path);
  strcat(client_buffer, " ");
  strcat(client_buffer, request->version);
  strcat(client_buffer, "\r\n");

  size_t length = strlen(client_buffer);

  if (ParsedHeader_set(request, "Connection", "close") < 0){
    perror("Error: Cannot set the connection");
  }

  if(ParsedHeader_get(request, "Host") == NULL){
    if(ParsedHeader_set(request, "Host", request->host) < 0){
       perror("Error: Cannot set the host");
    }
  }

  if (ParsedRequest_unparse_headers(request, client_buffer + length, (size_t)4096 - length) < 0) {
		perror("Error: Couldn't parse");
	}

  int port = 0; 

  if(request->port == NULL){
    port = 80;
  } else {
    port = atoi(request->port);
  }

  int server_socket = connectServer(request->host, port);
  if(server_socket < 0){
    return -1; 
  }

  int bytes_sent = send(server_socket, client_buffer, strlen(client_buffer), 0);
  bzero(client_buffer, 4096);
  bytes_sent = recv(clientSocket, client_buffer, 4095, 0);

  while(bytes_sent > 0) {
    bytes_sent = send(clientSocket, client_buffer, bytes_sent, 0);
    if(bytes_sent < 0) {
      perror("Error: cannot send data");
      break;
    }
    bzero(client_buffer, 4096);
    bytes_sent = recv(server_socket, client_buffer, 4095, 0);
  }

  bzero(client_buffer, 4096);
  close(server_socket);
  return 0; 
}


int checkHttpsRequest(char *request) {
  int version = 0; 
  if(strncmp(request, "HTTP/1.0", 8) == 0){
		version = 1;
  } else {
    version = -1; 
  }
  return version; 
}

int getMethod(char *method){
  int type = 0; 

  if(strncmp(method, "GET\0",4) == 0) {
    type = 1; 
  } else {
    type = -1; 
  }
  return type;
}


void respond(int socket) {
  char error_msg[1024];

  int bytes_sent; 
  int length; 
  char *buffer = (char*)calloc(4096,sizeof(char));
  bytes_sent = recv(socket, buffer, 4096, 0);
  
  while(bytes_sent > 0){
    length = strlen(buffer);
    if(strstr(buffer, "\r\n\r\n") == NULL) {
      bytes_sent = recv(socket, buffer + length, 4096 - length, 0);
    } else {
      break; 
    }
  }
    if(bytes_sent > 0) {
      length = strlen(buffer);
      // parse the headers 
      struct ParsedRequest *request = ParsedRequest_create();
      if(ParsedRequest_parse(request, buffer, length) < 0) {
        snprintf(error_msg, sizeof(error_msg),"HTTP/1.0 400 Bad Request\r\n\r\n");
        send(socket, error_msg, strlen(error_msg), 0);
      } else {
        bzero(buffer, 4096);
        //check the method type 
        int type = getMethod(request->method); 
        if(type == 1) {
          //handle the get request 
          if(request->host && request->path && (checkHttpsRequest(request->version) == 1)) {
            bytes_sent = handleRequest(socket, request, buffer);
            if(bytes_sent == -1){
              send(socket, error_msg, strlen(error_msg), 0);
            }
          }
        } else {
          //send the error message for any other methods 
          send(socket, error_msg, strlen(error_msg), 0);
        }
      }
      ParsedRequest_destroy(request);
    }
    if(bytes_sent < 0) {
      perror("Error: couldn't recieve data from client");
    } else if(bytes_sent == 0) {
      //client has closed the connection 
    }
    close(socket);
    free(buffer);
    return; 
}

int findChild(int i) {
  int j = i;
	pid_t ret_pid;
	int child_state;

	do
	{
		if(client_PID[j] == 0)
			return j;
		else
		{
			ret_pid = waitpid(client_PID[j], &child_state, WNOHANG);		// Finds status change of pid

			if(ret_pid == client_PID[j]){
				client_PID[j] = 0;
				return j;
        }
			else if(ret_pid == 0)											// Child is still running
			{
				;
			}
			else
				perror("Error in waitpid call\n");
		}
		j = (j+1)%MAX_CLIENTS;
	}
	while(j != i);
	return -1;
}

int proxy(char *proxy_port){

  int newSocket;
  int port = atoi(proxy_port);
  int socketId; 
  int client_len;
	struct sockaddr_in server_address;
  struct sockaddr_in client_address;
	bzero(client_PID, MAX_CLIENTS);

  //creating the actual socket
  socketId = socket(AF_INET,SOCK_STREAM,0);

  if(socketId < 0) {
    perror("Error: Couldn't create socket");
    exit(1);
  }

  //Bind the socket
  bzero((char*)&server_address, sizeof(server_address));
  server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = INADDR_ANY;

  int bind_value = bind(socketId, (struct sockaddr*)&server_address, sizeof(server_address));
	
  if(bind_value < 0) {
  	perror("Error : Port may not be free. Try Using diffrent port number.\n");
		exit(1);
	}

  //Listen for connection ad accept up to 100 clients 
  int status = listen(socketId, MAX_CLIENTS);

  if(status < 0) {
    perror("Error: Cannot listen to port");
    exit(1);
  }

  //start accepting connections 
  int i = 0; 
  int req = 0; 

  while(1){
    bzero((char*)&client_address, sizeof(client_address));
    client_len = sizeof(client_address);

    newSocket = accept(socketId, (struct sockaddr *) &client_address, (socklen_t*)&client_len);
    
    if(newSocket < 0) {
       perror("Error: Cannot create socket");
       exit(1);
    }

    //forking new clients

    i = findChild(i); 

    if(i >= 0 && i < MAX_CLIENTS){
      req = fork();
      if(req == 0){
        //creating the child process
        respond(newSocket);
        exit(0);
      } else {
        client_PID[i] = req;
      }
    } else {
      i = 0; 
      close(newSocket);
    }
  } 
close(socketId);
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