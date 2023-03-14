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
#include <unistd.h>
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
    int client_sockets[MAX_CLIENTS], tps_cur[MAX_CLIENTS];
    fd_set readfds;
    struct sockaddr_in server_addr, client_addr;

    // create proxy server socket
    if((proxy_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
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
        perror("listen failed\n");
        exit(1);
    }
    printf("Listen on port %d", listen_port);
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
    printf("Connect to Server.\n");
    // initialize the client sockets and throughputs
    for(int i = 0; i < MAX_CLIENTS; i++){
        client_sockets[i] = 0;
        tps_cur[i] = 0;
    }

    while(1){
        // clear the socket set
        FD_ZERO(&readfds);
        // add server socket to set
        FD_SET(proxy_server_socket, &readfds);
        for(int i = 0; i < MAX_CLIENTS; i++) {
            // if there is client sd, add to set
            sd = client_sockets[i];
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }
        }
        // wait for the socket activity
        activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

        // if something happened to server socket, accept the connection
        if(FD_ISSET(proxy_server_socket, &readfds)){
            int new_socket;
            if ((new_socket = accept(proxy_server_socket, (struct sockaddr*)&server_addr, (socklen_t*)sizeof(server_addr))) < 0){
                perror("accept error");
                exit(EXIT_FAILURE);
            }
            printf("New connection, socket fd is %d, IP is : %s, port : %d\n", new_socket, inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
            ssize_t send_ret = send(new_socket, buffer, strlen(buffer), 0);
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
                valread = recv(client_socket, buffer, MAX_BUFFER_SIZE, MSG_NOSIGNAL);
            }
            if (valread == 0){
                // somebody disconnect
                close(client_socket);
                client_sockets[i] = 0;
                tps_cur[i] = 0;
            }
            else{
//                // parse the http request
//                char *request_lines = strtok(buffer, "\r\n");
//
//                // make change to the http request
//                if (){
//                    // if f4m existed, make change to url and send two request to server
//
//                }
//                else if (){
//                    // if chunk request exists
//                }
//                else {
//                    // direct the request to the server directly
//                    if (send(proxy_client_socket, buffer, strlen(buffer), 0) < 0) {
//                        perror("proxy send to server failed");
//                        exit(EXIT_FAILURE);
//                    }
//                    // receive data from server
//                    memset(buffer, 0, MAX_BUFFER_SIZE);
//                    if (recv(proxy_client_socket, buffer, MAX_BUFFER_SIZE, MSG_NOSIGNAL) < 0) {
//                        perror("proxy receive chunks from server failed");
//                        exit(EXIT_FAILURE);
//                    }
//                    // direct the chunk to client
//                    if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
//                        perror("proxy send to server failed");
//                        exit(EXIT_FAILURE);
//                    }
//                    memset(buffer, 0, MAX_BUFFER_SIZE);
//                }
                // send the http request
                if (send(proxy_client_socket, buffer, strlen(buffer), 0) < 0) {
                        perror("proxy send to server failed");
                        exit(EXIT_FAILURE);
                    }
                // 从目标服务器接收HTTP响应并发送回客户端
                memset(buffer, 0, MAX_BUFFER_SIZE);

                   time_t start_t, end_t;
                int received = 0,recvbytes = 0;
                double rate = 0;
                while(1){
                    time(&start_t);
                    if((recvbytes = recv(proxy_client_socket, buffer, MAX_BUFFER_SIZE, 0)) == -1) {//接收客户端的请求
                        perror("receive failed");
                        return -1;
                    }
                    time(&end_t);
                    recvbytes /= 1000;
                    double total_t = difftime(end_t,start_t);
                    if(rate == 0){
                        rate = recvbytes*8/(1000*total_t);
                    }else{
                        rate = alpha*(recvbytes*8/(1000*total_t)) + (1 - alpha) * rate;
                    }
                    
                    printf("Thoughput=%.3f Mbps\n",rate);

                    if(recvbytes == 0){
                        break;
                    }
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