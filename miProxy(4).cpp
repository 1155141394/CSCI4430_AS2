#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/types.h>
#include <netdb.h>
#include <sys/time.h>
#include <errno.h>
#include <string.h>
#include <iostream>
#include <unistd.h>
#include <fstream>
#include <chrono>
#include <map>
using namespace std::chrono;
using namespace std;

// Feel free to change these constants if they are not right
#define TCP_PORT 80
#define PORT_TO_SERVER 1234
#define CHUNK_SIZE 32768
#define MAX_CLIENTS 16
#define MAX_SERVERS 16

typedef struct {
    char ready;
    int len;
    int bitrate_list[100];
} bitrate_cfg;

bitrate_cfg bitrate = {0, 0};

typedef struct {
    int index;
    int start_stream;
    double duration;
    double tput;
    double avg_tput;
    int b_rate;
    string browser_ip;
    string chunk_name;
    string server_ip;

} log_record;

int cmpfunc (const void * a, const void * b)
{
    return ( *(int*)a - *(int*)b );
}

void init_log(log_record *logRecord){
    logRecord->start_stream = 0;
}

double get_throughput(double time, int size, double alpha, int idx){
    /* return stored throughput if it is called in client_to_proxy(with size = -1)
     * * calculate new throughput if it is called in proxy_to_client(with size >= 0) */
    static double throughput[MAX_CLIENTS] = {0};

    if (size >= 0) {
        throughput[idx] = alpha * ((double) size * 8) / (1000 * time) + (1 - alpha) * throughput[idx]; // Kbps
    }
    return throughput[idx];
}

int find_path_pos(string str, int pos){
    while(str[pos] != '/'){
        pos --;
    }
    return pos;
}

int get_http_length(char *buffer){
    char* len_ptr_in_str = strstr(buffer, "Content-Length:");
    char* next_endl = strstr(len_ptr_in_str, "\r\n");

    int len_len = next_endl - len_ptr_in_str - 16;
    char *len_string = new char[len_len + 1];
    memset(len_string, 0, len_len + 1);
    strncpy(len_string, &len_ptr_in_str[16], len_len);

    char* header_len = strstr(buffer, "\r\n\r\n");
    return (header_len + 4 - buffer) + atoi(len_string);
}

int get_new_socket_as_server(sockaddr_in *addr, int port, const char* www_ip) {
    int yes = 1;
    int new_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = INADDR_ANY;
    addr->sin_port = htons((uint16_t) port);

    int success = bind(new_socket, (sockaddr*) addr, sizeof(*addr));
    listen(new_socket, 3);
    return new_socket;
}

int get_new_socket_as_client(sockaddr_in *addr, char* www_ip) {
    int yes = 1;
    int new_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    setsockopt(new_socket, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    hostent *server = gethostbyname(www_ip);
    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_addr.s_addr = *(unsigned int*)server->h_addr_list[0];
    addr->sin_port = htons((uint16_t) TCP_PORT);

    int success = connect(new_socket, (sockaddr*) addr, sizeof(*addr));
    return new_socket;
}

void parse_f4m(string buffer){
    int brate_pos, end_pos;
    brate_pos = buffer.find("bitrate");
    while (brate_pos != buffer.npos){
        end_pos = brate_pos + 1; // bitrate="
        while (buffer[end_pos] != '\"') {
            end_pos++;
        }
        bitrate.bitrate_list[bitrate.len++] = stoi(buffer.substr(brate_pos + 9, end_pos));
        buffer = buffer.substr(brate_pos + 9, buffer.length() - 1);
        brate_pos = buffer.find("bitrate");
    }
    qsort(bitrate.bitrate_list, bitrate.len, sizeof(int), cmpfunc);
    return;
}

int choose_bitrate(double throughput){
    throughput /= 1.5;
    int idx = 0;
    while (bitrate.bitrate_list[idx] <= throughput && idx < bitrate.len)
        idx++;
    return bitrate.bitrate_list[idx - 1];
}

void get_bitrate(char *ip, char *request){
    sockaddr_in addr;
    char buffer[CHUNK_SIZE + 1];
    int received = 0, total_size = -1;
    int new_socket = get_new_socket_as_client(&addr, ip);
    send(new_socket, request, strlen(request), 0);
    while (1){
        received += recv(new_socket, buffer, CHUNK_SIZE, 0);
        if (total_size == -1){
            total_size = get_http_length(buffer);
        }

        parse_f4m(string(buffer));

        if (received == total_size)
            break;
    }
    bitrate.ready = 1;
    close(new_socket);
}

// used by client_to_proxy
int edit_destination(char* buffer, int at, char* www_ip, int port, string bitrate_str) {
    string buf_tmp = buffer, result = "", tmp, keep_f4m = "";
    int f4m_pos, seg_pos, path_pos, flag = 0;

    tmp = buf_tmp.substr(0, at + 6);
    f4m_pos = tmp.find("big_buck_bunny.f4m");//18 characters
    seg_pos = tmp.find("Seg");
    if (f4m_pos != tmp.npos){

        result += tmp.substr(0, f4m_pos) + "big_buck_bunny_nolist.f4m" + tmp.substr(f4m_pos + 18, at + 6); //replace f4m
        keep_f4m += tmp;
        keep_f4m += www_ip;
        keep_f4m += ":" + to_string(port);
        flag = 1;

    } else if (seg_pos != tmp.npos){

        path_pos = find_path_pos(tmp, seg_pos);
        result += tmp.substr(0, path_pos + 1) + bitrate_str + tmp.substr(seg_pos, at + 6); //replace segment

    } else{
        result += tmp;
    }

    result += www_ip;
    result += ":" + to_string(port);

    int resume_pos = buf_tmp.find("\r\n", at);
    result += buf_tmp.substr(resume_pos);
    strcpy(buffer, result.c_str());
    cout << "parse result " << result << endl;
    if (flag && bitrate.ready == 0) {
        keep_f4m += buf_tmp.substr(resume_pos);
        char *keep_buf = new char[keep_f4m.length()];
        strcpy(keep_buf, keep_f4m.c_str());
        get_bitrate(www_ip, keep_buf);
    }

    return result.length();
}

// return 1 = the stream should be left open
// return 0 = the stream can be closed
int client_to_proxy(int to_client_socket, int to_server_socket, char* www_ip, log_record *tmp_log) {
    char* buffer = new char[CHUNK_SIZE + 1];
    int http_pos, name_pos;
    string buf_str;

    while (true) {
        //cout << "GET FROM CLIENT" << endl;
        int byte_received = recv(to_client_socket, buffer, CHUNK_SIZE, MSG_NOSIGNAL);
        //cout << "RECEIVED " << byte_received << " BYTES" << endl;

        if (byte_received <= 0) return 0;

        double throughput = get_throughput(0, -1, 0, tmp_log->index);
        int br = choose_bitrate(throughput);

        //cout << "THROUGHPUT " << throughput << endl;
        //cout << "BITRATE " << br << endl;
        tmp_log->b_rate = br;

        char* host_ptr_in_str = strstr(buffer, "Host:");
        int len = byte_received;
        if (host_ptr_in_str != NULL) {
            int host_pos_in_str = host_ptr_in_str - buffer;
            len = edit_destination(buffer, host_pos_in_str, www_ip, TCP_PORT, to_string(br));
        }

        buf_str = string(buffer);
        http_pos = buf_str.find(" HTTP/1.1");
        name_pos = find_path_pos(buf_str, http_pos);
        tmp_log -> chunk_name = buf_str.substr(name_pos + 1, (http_pos - name_pos) - 1);

        if (tmp_log->chunk_name.find("Seg") != tmp_log->chunk_name.npos && tmp_log->start_stream == 0)
            tmp_log->start_stream = 1;

        // direct modified request to server
        //cout << "SENDING TO SERVER" << endl;
        int t = send(to_server_socket, (void*) buffer, (size_t) len, MSG_NOSIGNAL);
        //cout << "SENT " << t << " BYTES" << endl;

        // since the client request only has the header,
        // we can check if the last 4 characters are \r\n\r\n
        // which indicates the end of the header
        if (len < 4) continue;
        char last_4_chars[5] = {0};
        strncpy(last_4_chars, &buffer[len - 4], 4);
        if (strcmp(last_4_chars, "\r\n\r\n") == 0) break;
    }
    cout << "END\n\n";

    return 1;
}

// return 1 = the stream should be left open
// return 0 = the stream can be closed
int proxy_to_client(int to_client_socket, int to_server_socket, double alpha, log_record *tmp_log) {
    //cout << "content from server" << endl;
    char* buffer = new char[CHUNK_SIZE + 1];
    double total_time = 0;
    int total_size = -1, current_size = 0;


    while (true) {
        //cout << "RECEIVING FROM SERVER" << endl;

        auto start = high_resolution_clock::now();
        int byte_received = recv(to_server_socket, buffer, CHUNK_SIZE, MSG_NOSIGNAL);
        total_time += duration<double>(high_resolution_clock::now() - start).count();

        cout << "RECEIVED " << byte_received << " BYTES" << endl;

        if (byte_received <= 0) return 0;

        // the first chunk of data should contain content length (must?)
        if (total_size == -1) {
            total_size = get_http_length(buffer);
        }

        //cout << "SENDING TO CLIENT" << endl;
        int t = send(to_client_socket, (void*) buffer, (size_t) byte_received, MSG_NOSIGNAL);
        cout << "SENT " << t << " BYTES" << endl;

        current_size += byte_received;
        if (current_size >= total_size) break;
        cout << "========================" << "current size " << current_size << "========================" << endl;
        cout << "========================" << " total size " << total_size << "========================" << endl;
    }
    if(tmp_log->start_stream){
        tmp_log->duration = total_time;
        tmp_log->avg_tput = get_throughput(total_time, current_size, alpha, tmp_log->index);
        tmp_log->tput = ((double) current_size * 8) / (1000 * total_time);
    }

    // cout << endl;
    cout << "END\n\n";

    return 1;
}

void proxy(int listen_port, char* www_ip, double alpha, string log) {
    sockaddr_in address_server, address_client;
    int addrlen_server = sizeof(address_server);
    int addrlen_client = sizeof(address_client);

    int to_client_socket = get_new_socket_as_server(&address_client, listen_port, "");
    int client_sockets[MAX_CLIENTS] = {0};
    int server_sockets[MAX_SERVERS] = {0};
    char *ip_table[MAX_CLIENTS];
    log_record *tmp_logs = new log_record[MAX_CLIENTS];
    for (int i = 0; i < MAX_CLIENTS; i++) {
        tmp_logs[i].start_stream = 0;
        tmp_logs[i].index = i;
    }

    fd_set readfds;

    ofstream outfile;
    outfile.open(log, ios::out | ios::trunc );
    outfile.close();

    /*
        logic:
        (1) receive sth from server -> send it to client (call proxy_to_client())
        (2) receive sth from client -> do the needed changes and send it to server (call client_to_proxy())

    */
    map<int, int> cli_to_ser, ser_to_cli;
    
    while (true) {
        FD_ZERO(&readfds);
        FD_SET(to_client_socket, &readfds);
        outfile.open(log, ios::out | ios::app );

        for (int i=0; i<MAX_SERVERS; i++) {
            if (server_sockets[i] != 0) {
                FD_SET(server_sockets[i], &readfds);
            }
        }
        for (int i=0; i<MAX_CLIENTS; i++) {
            if (client_sockets[i] != 0) {
                FD_SET(client_sockets[i], &readfds);
            }
        }

        int activity = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);

        if (activity < 0 && errno != EINTR) {
            cout << activity << " " << errno << " SELECT error" << endl;
            return;
        }

        if (FD_ISSET(to_client_socket, &readfds)) {
            int new_socket = accept(to_client_socket, (sockaddr*) &address_client, (socklen_t*) &addrlen_client);

            cout << "NEW client at " << ntohs(address_client.sin_port) << endl;
			
			int p = -1, q = -1;
			
            for (int i=0; i<MAX_CLIENTS; i++) {
                if (client_sockets[i] == 0) {
                    client_sockets[i] = new_socket;
                    ip_table[i] = inet_ntoa(address_client.sin_addr);
                    cout << ip_table[i] << endl;
                    p = i;
                    break;
                }
            }

            for (int i=0; i<MAX_SERVERS; i++) {
                if (server_sockets[i] == 0) {
                    server_sockets[i] = get_new_socket_as_client(&address_server, www_ip);
                    q = i;
                    break;
                }
            }
            cli_to_ser[p] = q;
            ser_to_cli[q] = p;
        }

        for (int i=0;i<MAX_CLIENTS; i++) {
            if (client_sockets[i] != 0 && FD_ISSET(client_sockets[i], &readfds)) {
            	int ser_pos = cli_to_ser[i];
            	
				//getpeername(client_sockets[i], (sockaddr*) &address_client, (socklen_t*) &addrlen_client);
                //getpeername(server_sockets[ser_pos], (sockaddr*) &address_server, (socklen_t*) &addrlen_server);
                
                int val = client_to_proxy(client_sockets[i], server_sockets[ser_pos], www_ip, &tmp_logs[ser_pos]);
                if (val == 0) {
                    tmp_logs[ser_pos].start_stream = 0;
                    client_sockets[i] = 0;
                    server_sockets[ser_pos] = 0;
                }
				break;
            }
        }
        
        for (int i=0; i<MAX_SERVERS; i++) {
            if (server_sockets[i] != 0 && FD_ISSET(server_sockets[i], &readfds)) {
				int cli_pos = ser_to_cli[i];
				
				sockaddr_in tmp_addr;
    			int addrlen_tmp = sizeof(tmp_addr);
    			
				getpeername(client_sockets[cli_pos], (sockaddr*) &tmp_addr, (socklen_t*) &addrlen_tmp);
                //getpeername(server_sockets[i], (sockaddr*) &address_server, (socklen_t*) &addrlen_server);
                
                int val = proxy_to_client(client_sockets[cli_pos], server_sockets[i], alpha, &tmp_logs[i]);
                if (val == 0) {
                	server_sockets[i] = 0;
                	client_sockets[cli_pos] = 0;
                }
                tmp_logs[i].browser_ip = inet_ntoa(tmp_addr.sin_addr);
                tmp_logs[i].server_ip = string(www_ip);
                if (tmp_logs[i].start_stream){
                    outfile << fixed << tmp_logs[i].browser_ip << " " << tmp_logs[i].chunk_name << " " << tmp_logs[i].server_ip
                            << " " << tmp_logs[i].duration << " " << tmp_logs[i].tput << " " << tmp_logs[i].avg_tput
                            << " " << tmp_logs[i].b_rate << endl;
                }
                break;
            }

        }
        outfile.close();

    }

}

int main(int argc, const char** argv) {
    if (argc != 6) {
        cout << "usage: ./miProxy --nodns <listen-port> <www-ip> <alpha> <log>" << endl;
        return 0;
    }

    int listen_port = atoi(argv[2]);

    int ip_len = strlen(argv[3]);
    char *www_ip = new char[ip_len + 1];
    strcpy(www_ip, argv[3]);

    double alpha = atof(argv[4]);
    string log = argv[5];

    proxy(listen_port, www_ip, alpha, log);
}