//
// Created by 徐涛 on 2023/3/12.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <time.h>
#include <sys/select.h>


#define MAX_CLIENTS 10
#define MAX_BUFFER_SIZE 2048
#define PORT 80

int main(int argc, char* argv[]){
    // read all the info from command line
    if(argc != 6){
        printf("The number of parameter in the command is not right.\n");
        exit(1);
    }
    char* flag = argv[1];
    int listen_port = atoi(argv[2]);
    char* ip = argv[3];
    double alpha = atof(argv[4]);
    char* log_addr = argv[5];
    char buffer[MAX_BUFFER_SIZE] = {0};

    // create variables
    int proxy_server_socket, proxy_client_socket;
    int max_sd, sd, activity, valread;
    int client_sockets[MAX_CLIENTS];
    fd_set readfds;
    struct sockaddr_in server_addr, client_addr;

    // create proxy server socket
    if((proxy_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP) < 0)){
        perror("socket creation failed");
        exit(1);
    }
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET; // Address family
    server_addr.sin_addr.s_addr = INADDR_ANY; // 0.0.0.0
    server_addr.sin_port = htons(listen_port); // Host to network long
    if(bind(proxy_server_socket, (struct sockaddr *) &server_addr, sizeof(server_addr)) < 0){
        perror("bind failed");
        exit(1);
    }
    if(listen(proxy_server_socket, 10) < 0 ){
        perror("listen failed");
        exit(1);
    }

    // create proxy client socket
    if((proxy_client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        perror("socket creation failed");
        exit(1);
    }
    
    // connect to the video server
    struct hostent *server = gethostbyname(ip);
    memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = * (unsigned long *) server->h_addr_list[0];
    client_addr.sin_port = htons(PORT);
    if(connect(proxy_client_socket, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0){
        perror("connection failed");
        exit(1);
    }

    // initialize the client sockets
    for(int i = 0; i < MAX_CLIENTS; i++){
        client_sockets[i] = 0;
    }

    while(1){
        // clear the socket set
        FD_ZERO(&readfds);
        // add server socket to set
        FD_SET(proxy_server_socket, &readfds);
        max_sd = proxy_server_socket;
        for(int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i];
            // if there is client sd, add to set
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
            // update max sd
            if (sd > max_sd) {
                max_sd = sd;
            }
        }
        // wait for the socket activity
        activity = select(max_sd+1, &readfds, NULL, NULL, NULL);

        // if something happened to server socket, accept the connection
        if(FD_ISSET(proxy_server_socket, &readfds)){
            int new_socket = connect(proxy_server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr));
            // add the new socket to client set
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // else it's some IO operation on a client socket
        for (int i = 0; i < MAX_CLIENTS; i++){
            int client_socket = client_sockets[i];
            if (client_socket != 0 && FD_ISSET(client_socket, &readfds)){
                valread = read(client_socket, buffer, MAX_BUFFER_SIZE);
            }
            if (valread == 0){
                // somebody disconnect
                close(client_socket);
                client_sockets[i] = 0;
            }
            else{
                // process the http request
                if (send(proxy_client_socket, buffer, strlen(buffer), 0) < 0) {
                        perror("proxy send to server failed");
                        exit(EXIT_FAILURE);
                    }
                // 从目标服务器接收HTTP响应并发送回客户端
                memset(buffer, 0, MAX_BUFFER_SIZE);

                if (recv(proxy_client_socket, buffer, MAX_BUFFER_SIZE, 0) < 0) {
                    perror("proxy recv failed");
                    exit(EXIT_FAILURE);
                }

                if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
                        perror("proxy send back to client failed");
                        exit(EXIT_FAILURE);
                }
                
                

            }
        }
    }


    return 0;
}