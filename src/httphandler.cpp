#include "httphandler.h"
char* http_handler::get_line(){
    return m_read_buf + m_start_line; 
}
void http_handler::init(int clientfd = 0,int epollfd = 0){
    m_clifd = clientfd;
    m_epollfd = epollfd;
    m_read_idx = 0;
    m_read_buf[0] = '\0';
    m_check_idx = 0;
    m_url= nullptr;m_version = nullptr;m_method = nullptr;
    m_check_state = CKECK_STATE_REQUESTLINE;
    m_start_line = 0;
    m_content_length = 0;
    m_keep_alive = false;
    m_host = nullptr;
    m_real_file = "";
    m_file_stat ={}; 
    m_file_address = nullptr;
    m_write_buf[0] = '\0';
    m_write_idx = 0;
    m_iov_count = 0;
    bytes_to_send = 0;
    bytes_have_sent = 0;
}
http_handler::http_handler(){
    init();
}
http_handler::http_handler(int clifd,int epollfd){
    int flags = fcntl(m_clifd, F_GETFL, 0);
    fcntl(m_clifd,F_SETFL,flags|O_NONBLOCK);
    epoll_event ev{};
    ev.events = EPOLLIN|EPOLLET;
    ev.data.fd = m_clifd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,m_clifd,&ev);
    init(clifd,epollfd);
}
bool http_handler::read_once(){
    while(true){
        int n = recv(m_clifd,m_read_buf+m_read_idx,READ_BUFFER_SIZE-m_read_idx,0);
        if(n==0)return false;
        else if(n>0){
            m_read_idx+=n;
            continue;
        }
        else if(errno==EAGAIN||errno==EWOULDBLOCK) break;
        else return false;
    }
    return true;
}
http_handler::LINE_STATUS http_handler::parse_line(){
    for(;m_check_idx<m_read_idx;++m_check_idx){
        char c = m_read_buf[m_check_idx];
        if(c=='\r'){
            if(m_check_idx+1==m_read_idx)return LINE_OPEN;
            if(m_read_buf[m_check_idx+1]=='\n'){
                m_read_buf[m_check_idx] = '\0';
                m_read_buf[m_check_idx+1] = '\0';
                m_check_idx+=2;
                return LINE_OK;
            }
            return LINE_BAD;
        } 
        else if(c=='\n'){
                m_read_buf[m_check_idx] = '\0';
                ++m_check_idx;
                return LINE_OK;
        }
    }
    return LINE_OPEN;
}
http_handler::HTTP_CODE http_handler::parse_request_line(char *text){
    char *url = std::strpbrk(text," \t");
    if (!url) return BAD_REQUEST;
    *url++ = '\0';
    char * version = std::strpbrk(url," \t");
    if (!version) return BAD_REQUEST;
    *version++ = '\0';
    char * method = text;
    while (*url == ' ' || *url == '\t') ++url;
    while (*version == ' ' || *version == '\t') ++version;
    m_method = method;
    m_url = url;
    m_version = version;
    if(!strcasecmp(m_method,"GET")&&!strcasecmp(m_method,"PUT"))return BAD_REQUEST;
    if(!strcasecmp(m_version,"HTTP/1.1")&&!strcasecmp(m_version,"HTTP/1.0"))return BAD_REQUEST;
    m_check_state = CKECK_STATE_HEADER;
    return NO_REQUEST;
}
http_handler::HTTP_CODE http_handler::parse_headers(char *text){
    if(text[0]=='\0'){
        if(m_content_length>0){
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        while(*text==' ')text++;
        if(strcasecmp(text,"keep-alive")==0) m_keep_alive = true;
    }
    if(strncasecmp(text,"Content-Length:",15)==0){
        text+=15;
        while(*text==' ')text++;
        m_content_length = atoi(text);
    }
    if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        while(*text==' ')text++;
        m_host = text;
    }
    return NO_REQUEST;
}
http_handler::HTTP_CODE http_handler::process_read(){
    LINE_STATUS line_state = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    while((m_check_state == CHECK_STATE_CONTENT && line_state == LINE_OK)||
        (line_state = parse_line()) == LINE_OK){
            char *text = get_line();
            m_start_line = m_check_idx;
            HTTP_CODE ret;
            switch (m_check_state)
            {
            case CKECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if(ret==BAD_REQUEST)return BAD_REQUEST;
                break;
            case CKECK_STATE_HEADER:
                ret = parse_headers(text);
                if(ret==BAD_REQUEST)return BAD_REQUEST;
                else if(ret==GET_REQUEST)return do_process();
                break;
            case CHECK_STATE_CONTENT:
                break;
            default:
                return INTERNAL_ERROR;
                break;
            }
        }
    return NO_REQUEST;
}
http_handler::HTTP_CODE http_handler::do_process(){
    std::string url = std::string(m_url).empty()?"/":m_url;
    if(url=="/")url = "/index.html";
    m_real_file = FILE_PATH+url;
    if(stat(m_real_file.c_str(),&m_file_stat)<0){
        return NO_SOURCE;
    }
    int fd = open(m_real_file.c_str(),O_RDONLY);
    if(fd<0)return NO_SOURCE;
    m_file_address = (char*)mmap(nullptr,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
    if(m_file_address==MAP_FAILED){
        m_file_address = nullptr;
        return INTERNAL_ERROR;
    }
    return FILE_REQUEST;
}
void http_handler::close_map(){
    if(m_file_address){
        munmap(m_file_address,m_file_stat.st_size);
        m_file_address = nullptr;
    }
}
bool http_handler::add_response(char* fmt,...){
    if(m_write_idx>=WRITE_BUFFER_SIZE)return false;
    va_list ap;
    va_start(ap,fmt);
    int n = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-m_write_idx,fmt,ap);
    va_end(ap);
    if(n<0||n>=WRITE_BUFFER_SIZE-m_write_idx)return false;
    m_write_idx+=n;
    return true;
}
bool http_handler::add_status_line(int status,const char *title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
} 
bool http_handler::add_headers(int content_len){
    return add_content_length(content_len)&&add_linger()&&add_blank_line();
}
bool http_handler::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);
}
bool http_handler::add_linger(){
    return add_response("Connection:%s\r\n","text/html");
}
bool http_handler::add_blank_line(){
    return add_response("%s","\r\n");
}
bool http_handler::add_content(const char *content){
    return add_response("%s",content);
}
bool http_handler::process_write(HTTP_CODE ret){
    const char *ok_200 = "OK";
    const char* err_400 = "Bad Request";
    const char* err_403 = "Forbidden";
    const char* err_404 = "Not Found";
    const char* err_500 = "Internal Error";

    const char* body_200 = "<html><body></body></html>";
    const char* body_400 = "<html><body><h1>400 Bad Request</h1></body></html>";
    const char* body_403 = "<html><body><h1>403 Forbidden</h1></body></html>";
    const char* body_404 = "<html><body><h1>404 Not Found</h1></body></html>";
    const char* body_500 = "<html><body><h1>500 Internal Error</h1></body></html>";
    if(ret==FILE_REQUEST){
        add_status_line(200,ok_200);
        if(m_file_stat.st_size!=0){
            add_headers(m_file_stat.st_size);
            iov[0].iov_base = m_write_buf;
            iov[0].iov_len = m_write_idx;
            iov[1].iov_base = m_file_address;
            iov[1].iov_len = m_file_stat.st_size;
            m_iov_count = 2;
            bytes_to_send = m_write_idx+m_file_stat.st_size;
            return true;
        }
        else{
            add_headers(strlen(body_200));
            if(!add_content(body_200))return false;
        }

    }
    else if(ret==BAD_REQUEST){
        add_status_line(400,err_400);
        add_headers(strlen(body_400));
        if(!add_content(body_400))return false;
    }
    else if(ret==NO_SOURCE){
        add_status_line(404,err_404);
        add_headers(strlen(body_404));
        if(!add_content(body_404))return false;
    }
    else if(ret==INTERNAL_ERROR){
        add_status_line(500,err_500);
        add_headers(strlen(body_500));
        if(!add_content(body_500))return false;
    }
    iov[0].iov_base = m_write_buf;
    iov[0].iov_len = m_write_idx;
    m_iov_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
bool http_handler::write(){
    int ret = 0;
    if(bytes_to_send==0){
        epoll_event ev{};
        ev.events = EPOLLIN|EPOLLET;
        ev.data.fd = m_clifd;
        epoll_ctl(m_epollfd,EPOLL_CTL_MOD,m_clifd,&ev);
        init(m_clifd,m_epollfd);
        return true;
    }
    while(1){
        ret = writev(m_clifd,iov,m_iov_count);
        if(ret<0){
            if(errno==EAGAIN){
                    epoll_event ev{};
                    ev.events = EPOLLOUT|EPOLLET;
                    ev.data.fd = m_clifd;
                    epoll_ctl(m_epollfd,EPOLL_CTL_MOD,m_clifd,&ev);
                return true;
            }
            return false;
        }
        bytes_have_sent+=ret;
        bytes_to_send-=ret;
        if(bytes_have_sent>=iov[0].iov_len){
            iov[0].iov_len = 0;
            iov[1].iov_base = m_file_address+(bytes_have_sent-m_write_idx);
            iov[1].iov_len = bytes_to_send;
        }
        else{
            iov[0].iov_base = m_write_buf+bytes_have_sent;
            iov[0].iov_len = iov[0].iov_len-bytes_have_sent;
        }
        if(bytes_to_send<=0){
            close_map();
            if(m_keep_alive){
                epoll_event ev{};
                ev.events = EPOLLIN|EPOLLET;
                ev.data.fd = m_clifd;
                epoll_ctl(m_epollfd,EPOLL_CTL_MOD,m_clifd,&ev);
                init(m_clifd,m_epollfd);
                return true;
            }
            else{
                return false;
            }
        }
    }
}
void http_handler::close_conn(){
    close_map();
    epoll_ctl(m_epollfd,EPOLL_CTL_DEL,m_clifd,0);
    init();
    close(m_clifd);
}
bool http_handler::process(){
    if(!read_once()){
        return false;
    }
    auto read_ret = process_read();
    if(read_ret==NO_REQUEST){
        epoll_event ev{};
        ev.events = EPOLLIN|EPOLLET;
        ev.data.fd = m_clifd;
        epoll_ctl(m_epollfd,EPOLL_CTL_MOD,m_clifd,&ev);
        return true;
    } 
    if(!process_write(read_ret)){
        return false;
    }
    if(!write())return false;
    return true;
}