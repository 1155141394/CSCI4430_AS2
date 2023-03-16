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
#include <ctype.h>


#define MAX_CLIENTS 10
#define MAX_BUFFER_SIZE 30000
#define PORT 80
#define MAX_REQUEST_LINE_LENGTH 1024


void create_log_file(char *browser_ip, char *chunkname, char *server_ip, double duration, double tput, double avg_tput, int bitrate, char *log) {
  FILE *fp;
  fp = fopen(log, "a"); // "w"表示以写入模式打开文件
  fprintf(fp, "%s %s %s %f %f %f %d\n", browser_ip, chunkname, server_ip, duration, tput, avg_tput, bitrate);
  fclose(fp);
}


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
    char browser_ip[MAX_CLIENTS][50];

    // create variables
    int proxy_server_socket, proxy_client_socket;
    int max_sd, sd, activity, valread;
    int client_sockets[MAX_CLIENTS];
    double tps_cur[MAX_CLIENTS];
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
                    strcpy(browser_ip[i],inet_ntoa(server_addr.sin_addr));
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
                        // send f4m
                        tran_request_without_sendback(buffer, f4m_file, valread, proxy_client_socket, proxy_server_socket);
                        extract_bitrate(f4m_file, bitrates);
                        memset(buffer, 0, MAX_BUFFER_SIZE);
                        tps_cur[i] = 0;
                        // send no_list.f4m request to server and transfer all the chunks to browser.
                        printf("%s\n", new_request);
                        tran_request(new_request, valread+7, proxy_client_socket, proxy_server_socket, client_socket);
                    }
                   else if (strstr(url, "Seg")) {
                        int tps_tmp = bitrates[0];
                        for(int k=0;k<50;k++){
                            //printf("%d\n",bitrates[k]);
                            if(bitrates[k] == 0){
                                break;
                            }
                            if(tps_cur[i]>=bitrates[k]*1.5){
                                tps_tmp = bitrates[k];
                                continue;
                            }else{
                                break;
                            }
                        }

                        char front_url[50];
                        char back_url[50];
                        char num[50];
                        int front_len = 0,back_len = 0, num_len = 0;
                        for(int k=0;k<strlen(url);k++){
                            if(isdigit(url[k])){
                                front_len = k;
                                break;
                            }else{
                                front_url[k] = url[k];
                            }
                        }
                        front_url[front_len] = '\0';
                        for(int k=front_len;k<strlen(url);k++){
                            if(isdigit(url[k])){
                                num[k-front_len] = url[k];
                            }else{
                                num_len = k-front_len;
                                break;
                            }
                        }
                        num[num_len] = '\0';
                        for(int k = front_len+num_len;k<strlen(url);k++){
                            back_url[k-front_len-num_len]=url[k];
                        }
                        back_len = strlen(url) - front_len -num_len;
                        back_url[back_len] = '\0';

                        char new_num[50] = {0};
                        snprintf(new_num, sizeof(new_num), "%d", tps_tmp);
                        char dep_num[50];
                        strcpy(dep_num,new_num);
                        int bitrt = atoi(new_num);
                        char* new_url = strcat(front_url,new_num);
                        //printf("%s,%s,%s\n",front_url,new_num,back_url);
                        new_url = strcat(new_url,back_url);
                        // printf("new url: %s\n",new_url);
                        char new_request[10000];
                        sprintf(new_request,"%s %s %s\r\n%s", method, new_url, version, request_rest);

                        int resp_header_len, resp_remain_len, cont_len;
                        // direct the request to the server directly
                        if (send(proxy_client_socket, new_request, valread-num_len+strlen(new_num), 0) < 0) {
                            perror("proxy send to server failed");
                            exit(EXIT_FAILURE);
                        }
                        time_t start_t;
                        int total_len = 0;
                        double total_t = 0;
                        start_t = clock();
                        // receive data from server
                        memset(buffer, 0, MAX_BUFFER_SIZE);
                        if ((valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE)) < 0) {
                            perror("proxy receive chunks from server failed");
                            exit(EXIT_FAILURE);
                        }
                        //total_t += (double)(clock() - start_t) / CLOCKS_PER_SEC;
                        
                        total_len+=valread;
                        //printf("Receive bytes: %d\n", valread);
                        buffer[valread] = '\0';
                        send(client_socket, buffer, valread, 0);

                        // get content length
                        cont_len = get_con_len(buffer);
                        //printf("Content length: %d\n", cont_len);
                        // get header length
                        resp_header_len = get_resp_header_len(buffer) + 1;
                        // printf("Response header lenght: %d\n", resp_header_len);
                        // printf("Buffer length: %zu\n", strlen(buffer));
                        // get remain content length
                        resp_remain_len = cont_len - (valread - resp_header_len);
                        memset(buffer, 0, MAX_BUFFER_SIZE);
                        //printf("Response remain length: %d\n", resp_remain_len);
                        // receive from the server if there is still sth
                        while(resp_remain_len > 0) {
                            //start_t = clock();
                            valread = read(proxy_client_socket, buffer, MAX_BUFFER_SIZE);
                            //total_t += (double)(clock() - start_t) / CLOCKS_PER_SEC;
                            total_len+=valread;
                            //printf("Receive bytes: %d\n", valread);
                            buffer[valread] = '\0';
                            resp_remain_len -= valread;
                            send(client_socket, buffer, valread, 0);
                            memset(buffer, 0, MAX_BUFFER_SIZE);
                            //printf("Response remain length: %d\n", resp_remain_len);
                        }
                        total_t = (double)(clock() - start_t) / 10000;
                        double T_new = total_len*8/(1000*total_t);
                        //printf("Rate=%.3f Kbps\n",T_new);
                        
                        create_log_file(browser_ip[i],strcat(new_num,back_url),ip,total_t,T_new,alpha * T_new + (1 - alpha) * tps_cur[i],bitrt,log_addr);
                        tps_cur[i] = alpha * T_new + (1 - alpha) * tps_cur[i];
                        
                        
                       

                   }
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