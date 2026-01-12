#pragma once
#include <sys/socket.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <string>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstdarg>
#include <sys/uio.h>
#include <fcntl.h>
const int READ_BUFFER_SIZE = 1024;
const int WRITE_BUFFER_SIZE = 1024;
const std::string FILE_PATH = "/home/lzf/wbserver/data";


class http_handler{
public:
    public:
    int m_epollfd;
    int m_read_idx = 0;
    int m_clifd;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_check_idx = 0;
    char *m_url,*m_version,*m_method;
    enum LINE_STATUS{LINE_OK,LINE_OPEN,LINE_BAD};
    enum HTTP_CODE{BAD_REQUEST,NO_REQUEST,GET_REQUEST,INTERNAL_ERROR,FILE_REQUEST,NO_SOURCE};
    enum CHECK_STATE{CKECK_STATE_REQUESTLINE,CKECK_STATE_HEADER,CHECK_STATE_CONTENT};

    CHECK_STATE m_check_state{CKECK_STATE_REQUESTLINE};
    int m_start_line = 0;
    int m_content_length = 0;
    bool m_keep_alive =false;
    char *m_host;
    std::string m_real_file;
    struct stat m_file_stat;
    char* m_file_address;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx = 0;
    struct iovec iov[2];
    int m_iov_count;
    int bytes_to_send;
    int bytes_have_sent;
    int RorW;
    char *get_line();
    void init();
    http_handler(int clientfd,int epollfd);
    bool read_once();
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    HTTP_CODE process_read();
    HTTP_CODE do_process();
    void close_map();
    bool add_response(char* fmt,...);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);
    bool process_write(HTTP_CODE ret);
    bool write();
    void close_conn();
    void process();
};