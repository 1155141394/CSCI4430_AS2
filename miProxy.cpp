#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/select.h>

#define MAX_CLIENTS 10 // 最大客户端数量
#define MAX_BUFFER_SIZE 1024 // 接收/发送缓冲区大小
#define SERVER_PORT 8888 // 代理服务器端口
#define SERVER_IP "127.0.0.1" // 代理服务器IP
#define TARGET_PORT 80 // 目标服务器端口
#define TARGET_IP "www.google.com" // 目标服务器IP

int main(int argc, char **argv) {
    int server_sockfd, target_sockfd;
    struct sockaddr_in server_addr, target_addr;
    int max_sd, sd, activity, valread;
    int client_sockets[MAX_CLIENTS];
    fd_set readfds;
    char buffer[MAX_BUFFER_SIZE] = {0};

    // 创建代理服务器socket
    if ((server_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 设置服务器socket选项
    if (setsockopt(server_sockfd, SOL_SOCKET, SO_REUSEADDR, &(int){ 1 }, sizeof(int)) < 0) {
        perror("setsockopt(SO_REUSEADDR) failed");
        exit(EXIT_FAILURE);
    }

    // 绑定服务器socket到指定IP和端口
    memset(&server_addr, '0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr.sin_port = htons(SERVER_PORT);

    if (bind(server_sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // 监听服务器socket，最多接受MAX_CLIENTS个客户端连接
    if (listen(server_sockfd, MAX_CLIENTS) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    // 创建目标服务器socket
    if ((target_sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // 连接到目标服务器
    memset(&target_addr, '0', sizeof(target_addr));
    target_addr.sin_family = AF_INET;
    target_addr.sin_port = htons(TARGET_PORT);

    if (inet_pton(AF_INET, TARGET_IP, &target_addr.sin_addr) <= 0) {
        perror("inet_pton failed");
        exit(EXIT_FAILURE);
    }

    if (connect(target_sockfd, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
        perror("connect failed");
        exit(EXIT_FAILURE);
    }

    // 初始化客户端socket数组
    for (int i = 0; i < MAX_CLIENTS; i++) {
        client_sockets[i] = 0;
    }

    while (1) {
        // 清空fd集合并将服务器socket添加进去
        FD_ZERO(&readfds);
        FD_SET(server_sockfd, &readfds);
        max_sd = server_sockfd;

        // 将客户端socket添加进fd集合中
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i];

            // 如果客户端socket有效，则添加进fd集合中
            if (sd > 0) {
                FD_SET(sd, &readfds);
            }

            // 更新最大socket描述符
            if (sd > max_sd) {
                max_sd = sd;
            }
        }

        // 等待任意fd有可读事件
        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            perror("select error");
            exit(EXIT_FAILURE);
        }

        // 如果服务器socket有可读事件，表示有新的客户端连接
        if (FD_ISSET(server_sockfd, &readfds)) {
            int new_socket;

            if ((new_socket = accept(server_sockfd, (struct sockaddr*)&server_addr, (socklen_t*)&addrlen)) < 0) {
                perror("accept error");
                exit(EXIT_FAILURE);
            }

            printf("New connection, socket fd is %d, IP is : %s, port : %d\n", new_socket, inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

            // 将新的客户端socket添加进客户端socket数组
            for (int i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    break;
                }
            }
        }

        // 处理客户端socket中的可读事件
        for (int i = 0; i < MAX_CLIENTS; i++) {
            sd = client_sockets[i];

            if (FD_ISSET(sd, &readfds)) {
                // 从客户端读取请求
                valread = read(sd, buffer, MAX_BUFFER_SIZE);

                // 如果客户端关闭连接，将其socket从客户端socket数组中移除
                if (valread == 0) {
                    getpeername(sd, (struct sockaddr*)&server_addr, (socklen_t*)&addrlen);
                    printf("Host disconnected, IP is : %s, port : %d\n", inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
                    close(sd);
                    client_sockets[i] = 0;
                }
                // 否则，处理HTTP请求并将其发送到目标服务器
                else {
                    // 将HTTP请求转发到目标服务器
                    if (send(target_sockfd, buffer, strlen(buffer), 0) < 0) {
                        perror("send failed");
                        exit(EXIT_FAILURE);
                    }

                    // 从目标服务器接收HTTP响应并发送回客户端
                    memset(buffer, 0, MAX_BUFFER_SIZE);

                    if (recv(target_sockfd, buffer, MAX_BUFFER_SIZE, 0) < 0) {
                        perror("recv failed");
                        exit(EXIT_FAILURE);
                    }

                    if (send(sd, buffer, strlen(buffer), 0) < 0) {
                        perror("send failed");
                        exit(EXIT_FAILURE);
                    }
                }
            }
        }
    }

    return 0;
}

