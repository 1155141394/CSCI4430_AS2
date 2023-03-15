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
#define MAX_BUFFER_SIZE 10000
#define PORT 80
#define MAX_REQUEST_LINE_LENGTH 1024

char* get_request_line(char* request) {
    char* end_of_request_line;
    static char request_line[MAX_REQUEST_LINE_LENGTH];

    // 获取请求行的结束位置
    end_of_request_line = strstr(request, "\r\n");

    if (end_of_request_line == NULL) {
        return NULL;
    }

    // 计算请求行的长度
    int request_line_length = end_of_request_line - request + strlen("\r\n");

    if (request_line_length >= MAX_REQUEST_LINE_LENGTH) {
        return NULL;
    }

    // 复制请求行到 request_line 变量中
    strncpy(request_line, request, request_line_length);
//    request_line[request_line_length] = '\0';

    return request_line;
}

char* get_rest_request(char* request) {
    char* end_of_request_line;
    static char request_rest[MAX_REQUEST_LINE_LENGTH];

    // 获取请求行的结束位置
    end_of_request_line = strstr(request, "\r\n");

    if (end_of_request_line == NULL) {
        return NULL;
    }

    // 计算请求行的长度
    int request_line_length = end_of_request_line - request + strlen("\r\n");

    // 将剩余请求中内容复制到新的字符串
    for(int i = request_line_length; i < strlen(request); i++) {
        request_rest[i-request_line_length] = request[i];
    }
    request_rest[strlen(request)-request_line_length] = '\0';
    return request_rest;
}

void extract_bitrate(char* str, int bitrates[]) {
    char* token = strtok(str, " ");
    int indx = 0;
    while (token != NULL) {
        if (strstr(token, "bitrate=") != NULL) {
//            printf("%s\n", token);
            char tmp[10] = {0};
            for(int i = 9; i < strlen(token)-1; i++) {
                tmp[i-9] = token[i];
            }
            bitrates[indx] = atoi(tmp);
            indx += 1;
        }
        token = strtok(NULL, " ");
    }
}

// get content length
int get_con_len(char* response) {
    int cont_len;
    char resp[10000];
    strcpy(resp,response);
    char* token = strtok(resp, "\r\n");
    char tmp[50] = {0};
    while (token)
    {
        if (strstr(token, "Content-Length")) {
            for(int i = 16; i < strlen(token); i++) {
                tmp[i-16] = token[i];
            }
            cont_len = atoi(tmp);
            break;
        }
        token = strtok(NULL, "\r\n");
    }
    return cont_len;

}

int get_resp_header_len(char* response){
    char resp[10000];
    strcpy(resp,response);
    int indx;
    for(int i = 3; i < strlen(resp); i++){
        if(resp[i] == '\n' && resp[i-1] == '\r' && resp[i-2] == '\n' && resp[i-3] == '\r' ){
            indx = i;
        }
    }
    return indx;
}

int tran_request(char* buffer, int valread, int proxy_client_socket, int proxy_server_socket, int client_socket) {
    int resp_header_len, resp_remain_len, cont_len;
    // direct the request to the server directly
    if (send(proxy_client_socket, buffer, valread, 0) < 0) {
        perror("proxy send to server failed");
        exit(EXIT_FAILURE);
    }
    // receive data from server
    memset(buffer, 0, MAX_BUFFER_SIZE);
    if ((valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE)) < 0) {
        perror("proxy receive chunks from server failed");
        exit(EXIT_FAILURE);
    }
    printf("Receive bytes: %d\n", valread);
    buffer[valread] = '\0';
    send(client_socket, buffer, valread, 0);

    // get content length
    cont_len = get_con_len(buffer);
    printf("Content length: %d\n", cont_len);
    // get header length
    resp_header_len = get_resp_header_len(buffer) + 1;
    printf("Response header lenght: %d\n", resp_header_len);
    printf("Buffer length: %zu\n", strlen(buffer));
    // get remain content length
    resp_remain_len = cont_len - (valread - resp_header_len);
    memset(buffer, 0, MAX_BUFFER_SIZE);
    printf("Response remain length: %d\n", resp_remain_len);
    // receive from the server if there is still sth
    while(resp_remain_len > 0) {
        valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE);
        printf("Receive bytes: %d\n", valread);
        buffer[valread] = '\0';
        resp_remain_len -= valread;
        send(client_socket, buffer, valread, 0);
        memset(buffer, 0, MAX_BUFFER_SIZE);
        printf("Response remain length: %d\n", resp_remain_len);
    }

}

char* tran_request_without_sendback(char* buffer, char res[], int valread, int proxy_client_socket, int proxy_server_socket) {
    int resp_header_len, resp_remain_len, cont_len;
    // direct the request to the server directly
    if (send(proxy_client_socket, buffer, valread, 0) < 0) {
        perror("proxy send to server failed");
        exit(EXIT_FAILURE);
    }
    // receive data from server
    memset(buffer, 0, MAX_BUFFER_SIZE);
    if ((valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE)) < 0) {
        perror("proxy receive chunks from server failed");
        exit(EXIT_FAILURE);
    }
    printf("Receive bytes: %d\n", valread);
    buffer[valread] = '\0';
    // send(client_socket, buffer, valread, 0);
    strcat(res, buffer);
    // get content length
    cont_len = get_con_len(buffer);
    printf("Content length: %d\n", cont_len);
    // get header length
    resp_header_len = get_resp_header_len(buffer) + 1;
    printf("Response header lenght: %d\n", resp_header_len);
    printf("Buffer length: %zu\n", strlen(buffer));
    // get remain content length
    resp_remain_len = cont_len - (valread - resp_header_len);
    memset(buffer, 0, MAX_BUFFER_SIZE);
    printf("Response remain length: %d\n", resp_remain_len);
    // receive from the server if there is still sth
    while(resp_remain_len > 0) {
        valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE);
        printf("Receive bytes: %d\n", valread);
        buffer[valread] = '\0';
        resp_remain_len -= valread;
        strcat(res, buffer);
//        send(client_socket, buffer, valread, 0);
        memset(buffer, 0, MAX_BUFFER_SIZE);
        printf("Response remain length: %d\n", resp_remain_len);
    }
    return res;
}

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
    int bitrates[50] = {0};
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
    printf("Listen on port %d\n", listen_port);
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
        printf("\n\nSelect start\n");
        activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

        // if something happened to server socket, accept the connection
        if(FD_ISSET(proxy_server_socket, &readfds)){
            int new_socket;
            socklen_t server_addr_len = sizeof(server_addr);
            if ((new_socket = accept(proxy_server_socket, (struct sockaddr*)&server_addr, & server_addr_len)) < 0){
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
            if (client_socket != 0 && FD_ISSET(client_socket, &readfds)) {
                valread = read(client_socket, buffer, MAX_BUFFER_SIZE);
                printf("%s\n", buffer);

                if (valread == 0){
                    // somebody disconnect
                    close(client_socket);
                    client_sockets[i] = 0;
                    tps_cur[i] = 0;
                }
                else {
                    // parse the http request
                    char* request_line = get_request_line(buffer);
                    char* request_rest = get_rest_request(buffer);
                    char method[100];
                    char url[100];
                    char version[100];
                    sscanf(request_line, "%s %s %s\r\n", method, url, version);
                    // make change to the http request
                    if (strstr(url, "f4m")) {
                        char f4m_file[10000] = {0};
                        // if f4m existed, make change to url and send two request to server
                        char* new_url = strcat(strtok(url ,"."), "_nolist.f4m");
                        char new_request[10000];
                        sprintf(new_request,"%s %s %s\r\n%s", method, new_url, version, request_rest);
                        printf("%s\n", new_request);
                        // send f4m
                        tran_request_without_sendback(buffer, f4m_file, sizeof(buffer), proxy_client_socket, proxy_server_socket);
                        extract_bitrate(f4m_file, bitrates);
                        memset(buffer, 0, MAX_BUFFER_SIZE);
                        for(int i = 0; i < 4; i++){
                            printf("Bit rates: %d\n", bitrates[i]);
                        }
                        // send no_list.f4m request to server and transfer all the chunks to browser.
                        printf("%s\n", new_request);
                        tran_request(new_request, strlen(new_request), proxy_client_socket, proxy_server_socket, client_socket);
                    }
//                    else if (strstr(url, )) {
//                        // if chunk request exists
                            // send the http request
//                            if (send(proxy_client_socket, buffer, strlen(buffer), 0) < 0) {
//                                perror("proxy send to server failed");
//                                exit(EXIT_FAILURE);
//                            }
//                            printf("%d\n", valread);
//                            // 从目标服务器接收HTTP响应并发送回客户端
//                            memset(buffer, 0, MAX_BUFFER_SIZE);
//
//                            time_t start_t, end_t;
//                            int recvbytes = 0;
//                            double rate = 0;
//                            while (1) {
//                                time(&start_t);
//                                if ((recvbytes = recv(proxy_client_socket, buffer, MAX_BUFFER_SIZE, 0)) == -1) {//接收客户端的请求
//                                    perror("receive failed");
//                                    return -1;
//                                }
//                                time(&end_t);
//                                recvbytes /= 1000; // KB
//                                double total_t = difftime(end_t, start_t);
//                                if (rate == 0) {
//                                    rate = recvbytes * 8 / (1000 * total_t); // MB
//                                } else {
//                                    rate = alpha * (recvbytes * 8 / (1000 * total_t)) + (1 - alpha) * rate;
//                                }
//
//                                printf("Thoughput=%.3f Mbps\n", rate);
//
//                                if (recvbytes == 0) {
//                                    break;
//                                }
//                            }
//

//                            if (send(client_socket, buffer, strlen(buffer), 0) < 0) {
//                                perror("proxy send back to client failed");
//                                exit(EXIT_FAILURE);
//                            }
        //
//                    }
                    else {
                        // direct the request to the server directly
                        tran_request(buffer, valread, proxy_client_socket, proxy_server_socket, client_socket);
                    }

                }
                

            }
        }
    }


    return 0;
}