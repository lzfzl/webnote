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
#include <iostream>
#include <unordered_map>
#include <mysql_driver.h>         
#include <mysql_connection.h>     
#include <cppconn/statement.h>     
#include <cppconn/prepared_statement.h>  
#include <cppconn/resultset.h>    
#include <cppconn/exception.h>     
#include <cppconn/metadata.h>  
#include <openssl/sha.h>
#include <openssl/rand.h>
#include <sstream>
#include <mutex>
#include <chrono>
#include "json.hpp"
const int READ_BUFFER_SIZE = 512*1024;
const int WRITE_BUFFER_SIZE = 128*1024;
const std::string FILE_PATH = "/home/lzf/wbserver/data";


class http_handler{
public:
    int m_epollfd;
    int m_read_idx = 0;
    int m_clifd;
    char m_read_buf[READ_BUFFER_SIZE];
    int m_check_idx = 0;
    std::string m_version, m_method;
    std::string m_url;
    enum LINE_STATUS{LINE_OK,LINE_OPEN,LINE_BAD};
    enum HTTP_CODE{DATA,BAD_REQUEST,NO_REQUEST,GET_REQUEST,INTERNAL_ERROR,FILE_REQUEST,NO_SOURCE,OPTIONS,WRONGLOGIN,SUCCESSLOGIN,SUCCESSSIGNUP,WRONGSIGNUP,CREATEPLAN,ADDPLAN,DELETEPLAN,UPDATEPLAN,SINGLEPLAN};
    enum CHECK_STATE{CKECK_STATE_REQUESTLINE,CKECK_STATE_HEADER,CHECK_STATE_CONTENT};
    CHECK_STATE m_check_state{CKECK_STATE_REQUESTLINE};
    int m_start_line = 0;
    int m_content_length = 0;
    bool m_keep_alive = false;
    std::string m_host;
    std::string m_real_file;
    struct stat m_file_stat;
    char* m_file_address;
    char m_write_buf[WRITE_BUFFER_SIZE];
    int m_write_idx = 0;
    std::string m_content_type;
    std::string m_boundary;
    std::string m_extra_headers;
    struct iovec iov[2];
    int m_iov_count;
    int bytes_to_send;
    int bytes_have_sent;
    int RorW;
    std::string cur_user;
    HTTP_CODE m_read_ret;
    std::unordered_map<std::string, std::string> m_post_params;
    std::string m_set_cookie;
    int m_body_received = 0;
    bool m_spool_to_disk = false;
    int m_tmp_fd = -1;
    std::string m_tmp_path;
    char *get_line();
    void init(int clientfd,int epollfd);
    http_handler(int clientfd,int epollfd);
    http_handler();
    bool read_once();
    LINE_STATUS parse_line();
    HTTP_CODE parse_request_line(char *text);
    HTTP_CODE parse_headers(char *text);
    void parseTable(char *text,int m_content_length);
    HTTP_CODE process_read();
    HTTP_CODE do_process();
    HTTP_CODE parse_body(char *text);
    std::string url_decode(const std::string& encoded);
    sql::Connection* sqlconn;
    sql::ResultSet* sqlres = nullptr;
    void close_map();
    bool add_response(const char* fmt,...);
    bool add_status_line(int status,const char *title);
    bool add_headers(int content_len);
    bool add_content_length(int content_len);
    bool add_linger();
    bool add_blank_line();
    bool add_content(const char *content);
    bool process_write();
    bool write();
    void close_conn();
    bool process();
    bool checkLogin();
    bool add_options_header();
    bool signUp();
    bool getUserData();
    bool addPlan();
    bool deletePlan();
    bool updatePlan();
    bool getSinglePlan();
};
