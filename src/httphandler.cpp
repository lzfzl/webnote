#include "httphandler.h"
#include <mutex>
#include <unordered_map>
#include <chrono>
#include "session_store.h"
#include <fstream>
#include <algorithm>
#include <string_view>

char* http_handler::get_line(){
    return m_read_buf + m_start_line; 
}
void http_handler::init(int clientfd = -100,int epollfd = -100){
    if(m_tmp_fd!=-1){
        close(m_tmp_fd);
        m_tmp_fd = -1;
    }
    if(!m_tmp_path.empty()){
        unlink(m_tmp_path.c_str());
        m_tmp_path.clear();
    }
    m_clifd = clientfd;
    m_epollfd = epollfd;
    m_read_idx = 0;
    m_read_buf[0] = '\0';
    m_check_idx = 0;
    m_url= "";
    m_version.clear();
    m_method.clear();
    m_check_state = CKECK_STATE_REQUESTLINE;
    m_start_line = 0;
    m_content_length = 0;
    m_keep_alive = false;
    m_host.clear();
    m_real_file = "";
    m_file_stat ={}; 
    m_file_address = nullptr;
    m_write_buf[0] = '\0';
    m_write_idx = 0;
    m_content_type ="";
    m_boundary="";
    m_extra_headers="";
    m_iov_count = 0;
    bytes_to_send = 0;
    bytes_have_sent = 0;
    m_read_ret = NO_REQUEST;
    cur_user = "";
    m_set_cookie="";
    m_body_received = 0;
    m_spool_to_disk = false;
    if(m_clifd!=0){
        int flags = fcntl(m_clifd, F_GETFL, 0);
        fcntl(m_clifd,F_SETFL,flags|O_NONBLOCK);
        epoll_event ev{};
        ev.events = EPOLLIN|EPOLLET;
        ev.data.fd = m_clifd;
        epoll_ctl(epollfd,EPOLL_CTL_ADD,m_clifd,&ev);
    }
}
http_handler::http_handler(){
    init();
}
http_handler::http_handler(int clifd,int epollfd){
    init(clifd,epollfd);
}
bool http_handler::read_once(){
    while(true){
        int to_read = READ_BUFFER_SIZE - m_read_idx;
        if(to_read<=0){
            if(m_check_state==CHECK_STATE_CONTENT && m_spool_to_disk){
                (void)parse_body(get_line());
                continue;
            }
            break;
        }
        int n = recv(m_clifd,m_read_buf+m_read_idx,to_read,0);
        // std::cout<<n;
        if(n==0){
            return false;}
        else if(n>0){
            m_read_idx+=n;
            continue;
        }
        else if(errno==EAGAIN||errno==EWOULDBLOCK) break;
        else {
            // std::cout<<errno;
            return false;}
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
    if (strcasecmp(m_method.c_str(), "GET") == 0) { 
        size_t query_pos = m_url.find('?');
        if (query_pos != std::string::npos) {
            std::string query_str = m_url.substr(query_pos + 1);
            m_url = m_url.substr(0, query_pos);
            size_t start = 0;
            size_t amp_pos = query_str.find('&');
            while (start < query_str.length()) {
                size_t end = (amp_pos == std::string::npos) ? query_str.length() : amp_pos;
                std::string kv_pair = query_str.substr(start, end - start);
                size_t eq_pos = kv_pair.find('=');
                if (eq_pos != std::string::npos) {
                    std::string key = kv_pair.substr(0, eq_pos);
                    std::string value = kv_pair.substr(eq_pos + 1);
                    value = url_decode(value);
                    m_post_params[key] = value;
                } else if (!kv_pair.empty()) {
                    m_post_params[kv_pair] = "";
                }
                start = end + 1;
                amp_pos = query_str.find('&', start);
            }
        }
    }
    if(strcasecmp(m_method.c_str(),"GET")
        && strcasecmp(m_method.c_str(),"PUT")
        && strcasecmp(m_method.c_str(),"OPTIONS")
        && strcasecmp(m_method.c_str(),"POST")
        && strcasecmp(m_method.c_str(),"DELETE")) return BAD_REQUEST;
    if(strcasecmp(m_version.c_str(),"HTTP/1.1")!=0&&strcasecmp(m_version.c_str(),"HTTP/1.0")!=0)return BAD_REQUEST;
    m_check_state = CKECK_STATE_HEADER;
    return NO_REQUEST;
}
http_handler::HTTP_CODE http_handler::parse_headers(char *text){
    // std::cout<<text;
    if(text[0]=='\0'){
        if(m_content_length>0){
            m_check_state = CHECK_STATE_CONTENT;
            m_spool_to_disk = (!m_content_type.empty() && m_content_type.find("multipart/form-data")!=std::string::npos) || m_content_length > READ_BUFFER_SIZE;
            m_body_received = 0;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }
    if(strncasecmp(text,"Connection:",11)==0){
        text+=11;
        while(*text==' ')text++;
        if(!strcasecmp(m_version.c_str(),"HTTP/1.1")||!strcasecmp(text,"keep-alive")) m_keep_alive = true;
        if(strcasecmp(text,"close")==0)m_keep_alive = false;
    }
    else if(strncasecmp(text,"Content-Length:",15)==0){
        text+=15;
        while(*text==' ')text++;
        m_content_length = atoi(text);
    }
    else if(strncasecmp(text,"Host:",5)==0){
        text+=5;
        while(*text==' ')text++;
        m_host = text;
    }
    else if(strncasecmp(text,"Content-Type:",13)==0){
        text+=13;
        while(*text==' ')text++;
        m_content_type = text;
        size_t p = m_content_type.find("boundary=");
        if(p!=std::string::npos){
            std::string b = m_content_type.substr(p+9);
            if(!b.empty() && b.front()=='"'){
                size_t q = b.find('"',1);
                if(q!=std::string::npos) b = b.substr(1,q-1);
            }
            m_boundary = b;
        }
    }
    else if(strncasecmp(text,"Cookie:",7)==0){
        text+=7;
        while(*text==' ')text++;
        std::string ck = text;
        std::string key, val;
        size_t i=0,n=ck.size();
        while(i<n){
            while(i<n && (ck[i]==' '||ck[i]==';')) ++i;
            size_t k=i;
            while(i<n && ck[i]!='=' && ck[i]!=';' && ck[i]!='\r' && ck[i]!='\n') ++i;
            key = ck.substr(k, i-k);
            if(i<n && ck[i]=='='){ ++i; size_t v=i; while(i<n && ck[i]!=';' && ck[i]!='\r' && ck[i]!='\n') ++i; val = ck.substr(v, i-v); }
            if(key=="session"){
                std::lock_guard<std::mutex> lk(g_sess_mtx);
                auto it = g_sessions.find(val);
                if(it!=g_sessions.end()){
                    long long t = now_sec();
                    if(it->second.exp>t){
                        cur_user = it->second.user;
                        it->second.exp = t + 7LL*24*3600;
                    }else{
                        g_sessions.erase(it);
                    }
                }
            }
        }
    }
    else{// ignore others
        while (*text != '\0' && *text != '\n') {
            text++;
        }
        if (*text == '\n') {
            text++;
        }
        while (*text == ' ' || *text == '\t' || *text == '\r') {
            text++;
        }
    }    
    return NO_REQUEST;
}
void http_handler::parseTable(char *text,int m_content_length){
    m_post_params.clear();
    if(m_content_length<=0||text==nullptr){
        return;
    }
    std::string body(text,m_content_length);
    using namespace nlohmann;
        try {
        // 4. 解析 JSON 字符串（异常捕获，防止解析失败崩溃）
        json json_data = json::parse(body);

        // 5. 遍历 JSON 中的所有键值对（模拟原函数的循环解析逻辑）
        for (auto it = json_data.begin(); it != json_data.end(); ++it) {
            std::string key = it.key();          // 获取 JSON 键
            std::string value;                   // 存储 JSON 值（统一转为字符串）

            // 6. 处理不同类型的 JSON 值（保证兼容数字/布尔/字符串等类型）
            if (it.value().is_string()) {
                value = it.value().get<std::string>();
            } else if (it.value().is_number()) {
                value = std::to_string(it.value().get<double>()); // 数字转字符串
            } else if (it.value().is_boolean()) {
                value = it.value().get<bool>() ? "true" : "false"; // 布尔转字符串
            } else {
                continue; // 跳过对象/数组等复杂类型（按需扩展）
            }

            // 7. URL 解码（和原 parseTable 逻辑一致，可选）
            key = url_decode(key);
            value = url_decode(value);

            // 8. 非空键存入 m_post_params（和原函数一致）
            if (!key.empty()) {
                m_post_params[key] = value;
            }
        }
    } catch (const json::parse_error& e) {
        // JSON 格式错误（比如语法错误、括号不匹配），静默返回（和原函数容错逻辑一致）
        std::cerr << "JSON 解析失败：" << e.what() << " 错误位置：" << e.byte << std::endl;
        return;
    } catch (const std::exception& e) {
        // 其他未知异常，容错处理
        std::cerr << "解析 JSON 异常：" << e.what() << std::endl;
        return;
    }
    // size_t pos = 0;
    // size_t len = body.size();
    // while(pos<len){
    //     size_t amp_pos = body.find('&',pos);
    //     std::string param_pair = (amp_pos ==std::string::npos)
    //     ? body.substr(pos)
    //     :body.substr(pos,amp_pos-pos);
    //     pos = (amp_pos == std::string::npos) ? len : amp_pos + 1;
    //     size_t eq_pos = param_pair.find('=');
    //     if (eq_pos == std::string::npos) {
    //         continue; 
    //     }
    //     std::string key = param_pair.substr(0, eq_pos);
    //     std::string value = param_pair.substr(eq_pos + 1);
    //     key = url_decode(key);
    //     value = url_decode(value);

    //     if (!key.empty()) {
    //         m_post_params[key] = value;
    //     }
    // }
}
std::string http_handler::url_decode(const std::string& encoded){
    std::string decoded;
    size_t i = 0;
    const size_t len = encoded.size();
    while (i < len) {
        if (encoded[i] == '+') {
            // +替换为空格（表单格式的空格编码）
            decoded += ' ';
            i++;
        } else if (encoded[i] == '%' && i + 2 < len) {
            // %xx格式的十六进制编码（如%E5%BC%A0%E4%B8%89 → 张三）
            std::string hex = encoded.substr(i + 1, 2);
            // 校验十六进制字符（0-9, a-f, A-F）
            if (!isxdigit(hex[0]) || !isxdigit(hex[1])) {
                decoded += encoded[i];
                i++;
                continue;
            }
            // 转换为字符
            char c = static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));
            decoded += c;
            i += 3;
        } else {
            // 普通字符直接保留
            decoded += encoded[i];
            i++;
        }
    }
    return decoded;
}


http_handler::HTTP_CODE http_handler::parse_body(char *text){
    if(m_spool_to_disk){
        int avail = m_read_idx - m_check_idx;
        if(avail>0){
            if(m_tmp_fd<0){
                std::string base = "/home/lzf/wbserver/userdata/tmp";
                struct stat st{};
                if(stat(base.c_str(), &st)!=0){
                    mkdir("/home/lzf/wbserver/userdata", 0755);
                    mkdir(base.c_str(), 0755);
                }
                char buf[128];
                snprintf(buf,sizeof(buf),"/home/lzf/wbserver/userdata/tmp/up_%d_%lld.bin",m_clifd,(long long)now_sec());
                m_tmp_path = buf;
                m_tmp_fd = open(m_tmp_path.c_str(), O_CREAT|O_TRUNC|O_WRONLY, 0600);
                if(m_tmp_fd<0) return INTERNAL_ERROR;
            }
            ssize_t wr = ::write(m_tmp_fd, m_read_buf + m_check_idx, avail);
            if(wr<0) return INTERNAL_ERROR;
            m_body_received += (int)wr;
            m_check_idx = m_read_idx;
            m_read_idx = 0;
            m_check_idx = 0;
            m_start_line = 0;
        }
        if(m_body_received < m_content_length) return NO_REQUEST;
        if(m_tmp_fd>=0){
            close(m_tmp_fd);
            m_tmp_fd = -1;
        }
        int fd = open(m_tmp_path.c_str(), O_RDONLY);
        if(fd<0) return INTERNAL_ERROR;
        size_t len = (size_t)m_content_length;
        char* data = (char*)mmap(nullptr, len, PROT_READ, MAP_PRIVATE, fd, 0);
        if(data==MAP_FAILED){
            close(fd);
            return INTERNAL_ERROR;
        }
        m_post_params.clear();
        std::string delim = "--"+m_boundary;
        std::string body(data, len);
        size_t pos = 0;
        size_t cur = body.find(delim, pos);
        if(cur==std::string::npos){
            munmap(data, len);
            close(fd);
            unlink(m_tmp_path.c_str());
            m_tmp_path.clear();
            return GET_REQUEST;
        }
        cur += delim.size();
        std::vector<std::string> saved_names;
        while(true){
            if(cur < body.size() && body.substr(cur,2)=="\r\n") cur+=2;
            if(cur+2<=body.size() && body.substr(cur,2)=="--") break;
            size_t hdr_end = body.find("\r\n\r\n", cur);
            if(hdr_end==std::string::npos) break;
            std::string headers = body.substr(cur, hdr_end-cur);
            size_t next_delim = body.find(delim, hdr_end+4);
            if(next_delim==std::string::npos) break;
            size_t data_end = next_delim;
            if(data_end>=2 && body.substr(data_end-2,2)=="\r\n") data_end-=2;
            std::string content_sv = body.substr(hdr_end+4, data_end-(hdr_end+4));
            std::string name, filename;
            std::string key = "Content-Disposition:";
            size_t p = headers.find(key);
            if(p!=std::string::npos){
                size_t nl = headers.find("\r\n", p);
                std::string cd(std::string(headers.substr(p, nl==std::string::npos?std::string::npos:nl-p)));
                size_t npos = cd.find("name=");
                if(npos!=std::string::npos){
                    size_t s = npos+5;
                    if(s<cd.size() && (cd[s]=='"'||cd[s]=='\'')){
                        char q = cd[s++];
                        size_t e = cd.find(q, s);
                        if(e!=std::string::npos) name = cd.substr(s, e-s);
                    }else{
                        size_t e = cd.find(';', s);
                        name = cd.substr(s, e==std::string::npos?std::string::npos:e-s);
                    }
                }
                size_t fpos = cd.find("filename=");
                if(fpos!=std::string::npos){
                    size_t s = fpos+9;
                    if(s<cd.size() && (cd[s]=='"'||cd[s]=='\'')){
                        char q = cd[s++];
                        size_t e = cd.find(q, s);
                        if(e!=std::string::npos) filename = cd.substr(s, e-s);
                    }else{
                        size_t e = cd.find(';', s);
                        filename = cd.substr(s, e==std::string::npos?std::string::npos:e-s);
                    }
                }
            }
            if(!filename.empty()){
                std::string ext;
                size_t dot = filename.find_last_of('.');
                if(dot!=std::string::npos){
                    ext = filename.substr(dot+1);
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                }
                bool ok = (ext=="doc"||ext=="docx"||ext=="pdf"||ext=="jpg"||ext=="jpeg"||ext=="png");
                if(ok){
                    std::string user = cur_user.empty() ? std::string("guest") : cur_user;
                    std::string dir = "/home/lzf/wbserver/userdata/"+user;
                    struct stat st{};
                    if(stat(dir.c_str(), &st)!=0){
                        mkdir("/home/lzf/wbserver/userdata", 0755);
                        mkdir(dir.c_str(), 0755);
                    }
                    std::string path = dir + "/" + filename;
                    std::ofstream ofs(path, std::ios::binary);
                    ofs.write(content_sv.data(), (std::streamsize)content_sv.size());
                    ofs.close();
                    saved_names.push_back(filename);
                }
            }else{
                std::string val = content_sv;
                if(name=="deadline"){
                    for(char &ch: val){ if(ch=='T') ch=' '; }
                    if(val.size()==16) val += ":00";
                }
                m_post_params[name] = val;
            }
            cur = next_delim + delim.size();
            if(cur < body.size() && body.substr(cur,2)=="--") break;
        }
        if(!saved_names.empty()){
            std::string all;
            for(size_t i=0;i<saved_names.size();++i){
                if(i) all.push_back(';');
                all.append(saved_names[i]);
            }
            m_post_params["filepth"] = all;
        }
        munmap(data, len);
        close(fd);
        unlink(m_tmp_path.c_str());
        m_tmp_path.clear();
        m_check_idx+=m_content_length;
        return GET_REQUEST;
    }
    if(m_read_idx>=m_content_length+m_check_idx){
        if(!m_content_type.empty() && m_content_type.find("multipart/form-data")!=std::string::npos && !m_boundary.empty()){
            m_post_params.clear();
            std::string body(text,m_content_length);
            std::string delim = "--"+m_boundary;
            size_t pos = 0;
            size_t cur = body.find(delim, pos);
            if(cur==std::string::npos){
                m_check_idx+=m_content_length;
                return GET_REQUEST;
            }
            cur += delim.size();
            std::vector<std::string> saved_names;
            while(true){
                if(cur < body.size() && body.compare(cur,2,"\r\n")==0) cur+=2;
                if(cur+2<=body.size() && body.compare(cur,2,"--")==0) break;
                size_t hdr_end = body.find("\r\n\r\n", cur);
                if(hdr_end==std::string::npos) break;
                std::string headers = body.substr(cur, hdr_end-cur);
                size_t next_delim = body.find(delim, hdr_end+4);
                if(next_delim==std::string::npos) break;
                size_t data_end = next_delim;
                if(data_end>=2 && body.compare(data_end-2,2,"\r\n")==0) data_end-=2;
                std::string content = body.substr(hdr_end+4, data_end-(hdr_end+4));
                std::string name, filename;
                {
                    std::string key = "Content-Disposition:";
                    size_t p = headers.find(key);
                    if(p!=std::string::npos){
                        size_t nl = headers.find("\r\n", p);
                        std::string cd = headers.substr(p, nl==std::string::npos?std::string::npos:nl-p);
                        size_t npos = cd.find("name=");
                        if(npos!=std::string::npos){
                            size_t s = npos+5;
                            if(s<cd.size() && (cd[s]=='"'||cd[s]=='\'')){
                                char q = cd[s++];
                                size_t e = cd.find(q, s);
                                if(e!=std::string::npos) name = cd.substr(s, e-s);
                            }else{
                                size_t e = cd.find(';', s);
                                name = cd.substr(s, e==std::string::npos?std::string::npos:e-s);
                            }
                        }
                        size_t fpos = cd.find("filename=");
                        if(fpos!=std::string::npos){
                            size_t s = fpos+9;
                            if(s<cd.size() && (cd[s]=='"'||cd[s]=='\'')){
                                char q = cd[s++];
                                size_t e = cd.find(q, s);
                                if(e!=std::string::npos) filename = cd.substr(s, e-s);
                            }else{
                                size_t e = cd.find(';', s);
                                filename = cd.substr(s, e==std::string::npos?std::string::npos:e-s);
                            }
                        }
                    }
                }
                if(!filename.empty()){
                    std::string ext;
                    size_t dot = filename.find_last_of('.');
                    if(dot!=std::string::npos) {
                        ext = filename.substr(dot+1);
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    }
                    bool ok = (ext=="doc"||ext=="docx"||ext=="pdf"||ext=="jpg"||ext=="jpeg"||ext=="png");
                    if(ok){
                        std::string user = cur_user.empty() ? std::string("guest") : cur_user;
                        std::string dir = "/home/lzf/wbserver/userdata/"+user;
                        struct stat st{};
                        if(stat(dir.c_str(), &st)!=0){
                            mkdir("/home/lzf/wbserver/userdata", 0755);
                            mkdir(dir.c_str(), 0755);
                        }
                        std::string path = dir + "/" + filename;
                        std::ofstream ofs(path, std::ios::binary);
                        ofs.write(content.data(), (std::streamsize)content.size());
                        ofs.close();
                        saved_names.push_back(filename);
                    }
                }else{
                    std::string val = content;
                    if(name=="deadline"){
                        for(char &ch: val){ if(ch=='T') ch=' '; }
                        if(val.size()==16) val += ":00";
                    }
                    m_post_params[name] = val;
                }
                cur = next_delim + delim.size();
                if(cur < body.size() && body.compare(cur,2,"--")==0) break;
            }
            if(!saved_names.empty()){
                std::string all;
                for(size_t i=0;i<saved_names.size();++i){
                    if(i) all.push_back(';');
                    all.append(saved_names[i]);
                }
                m_post_params["filepth"] = all;
            }
        }else{
            parseTable(text,m_content_length);
        }
        m_check_idx+=m_content_length;
        return GET_REQUEST;
    }
    else return NO_REQUEST;
}

http_handler::HTTP_CODE http_handler::process_read(){
    LINE_STATUS line_state = LINE_OK;
    while(true){
        if(m_check_state == CHECK_STATE_CONTENT){
            char *text = get_line();
            m_start_line = m_check_idx;
            HTTP_CODE ret = parse_body(text);
            if(ret==GET_REQUEST) return do_process();
            return NO_REQUEST;
        }
        line_state = parse_line();
        if(line_state != LINE_OK) break;
        char *text = get_line();
        m_start_line = m_check_idx;
        HTTP_CODE ret;
        switch (m_check_state){
            case CKECK_STATE_REQUESTLINE:
                ret = parse_request_line(text);
                if(ret==BAD_REQUEST) return BAD_REQUEST;
                break;
            case CKECK_STATE_HEADER:
                ret = parse_headers(text);
                if(ret==BAD_REQUEST) return BAD_REQUEST;
                else if(ret==GET_REQUEST) return do_process();
                break;
            default:
                return INTERNAL_ERROR;
        }
    }
    return NO_REQUEST;
}
http_handler::HTTP_CODE http_handler::do_process(){
    if(strcasecmp(m_method.c_str(),"OPTIONS")== 0){
        return OPTIONS;
    }
    std::string url = std::string(m_url).empty()?"/":m_url;
    if(url=="/")url = "/index.html";
    else if(url=="/api/signup"){
        if(strcasecmp(m_method.c_str(),"OPTIONS")== 0)return OPTIONS;
        if(strcasecmp(m_method.c_str(),"POST")== 0){
            if(signUp()){
                return SUCCESSSIGNUP;
            }
            else{
                return WRONGSIGNUP;
            }
        }
    }
    else if(url=="/api/loginbutton"){
        if(strcasecmp(m_method.c_str(),"OPTIONS")== 0)return OPTIONS;
        if(strcasecmp(m_method.c_str(),"POST")== 0){
            if(checkLogin()){
                return SUCCESSLOGIN;
            }
            else{
                return WRONGLOGIN;
            }
        }
    }
    else if(url =="/api/purchase-plans"){
        if(strcasecmp(m_method.c_str(),"POST")==0){
            return ADDPLAN;
        }
        return DATA;
    }
    else if(url =="/api/purchase-plans/create"){
        return CREATEPLAN;
    }
    else if(url =="/api/alterbutton"){

    }
    else if(url.size() > std::string("/api/purchase-plans/").size() 
            && url.compare(0, std::string("/api/purchase-plans/").size(), "/api/purchase-plans/") == 0){
        const std::string prefix = "/api/purchase-plans/";
        std::string tail = url.substr(prefix.size());
        if(!tail.empty() && tail.find('/') == std::string::npos){
            bool all_digits = true;
            for(char c: tail){ if(c<'0' || c>'9'){ all_digits = false; break; } }
            if(all_digits && strcasecmp(m_method.c_str(),"DELETE")==0){
                m_post_params["planid"] = tail;
                return DELETEPLAN;
            }
        }
    }
    else if(url =="/api/downfile"){
        if(strcasecmp(m_method.c_str(),"GET")!=0) return BAD_REQUEST;
        std::string planid = m_post_params["planid"];
        std::string filename = m_post_params["filename"];
        if(cur_user.empty() || planid.empty() || filename.empty()) return BAD_REQUEST;
        sql::PreparedStatement* pstmt = nullptr;
        sql::ResultSet* rs = nullptr;
        try{
            pstmt = sqlconn->prepareStatement("SELECT filepth FROM plan WHERE username=? AND planid=?");
            pstmt->setString(1, cur_user);
            pstmt->setInt(2, std::stoi(planid));
            rs = pstmt->executeQuery();
            std::string filelist;
            if(rs->next()){
                filelist = rs->getString("filepth");
            }else{
                if(rs) delete rs;
                if(pstmt) delete pstmt;
                return NO_SOURCE;
            }
            if(rs) delete rs;
            if(pstmt) delete pstmt;
            bool allowed = false;
            size_t start = 0;
            while(true){
                size_t pos = filelist.find(';', start);
                std::string f = filelist.substr(start, pos==std::string::npos?std::string::npos:pos-start);
                if(f==filename){ allowed = true; break; }
                if(pos==std::string::npos) break;
                start = pos+1;
            }
            if(!allowed) return NO_SOURCE;
            std::string path = std::string("/home/lzf/wbserver/userdata/")+cur_user+"/"+filename;
            if(stat(path.c_str(), &m_file_stat)<0) return NO_SOURCE;
            int fd = open(path.c_str(), O_RDONLY);
            if(fd<0) return NO_SOURCE;
            m_file_address = (char*)mmap(nullptr,m_file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);
            if(m_file_address==MAP_FAILED){
                m_file_address = nullptr;
                return INTERNAL_ERROR;
            }
            m_extra_headers = "Content-Type: application/octet-stream\r\nContent-Disposition: attachment; filename=\""+filename+"\"\r\n";
            return FILE_REQUEST;
        }catch(...){
            if(rs) delete rs;
            if(pstmt) delete pstmt;
            return INTERNAL_ERROR;
        }
    }
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
bool http_handler::add_response(const char* fmt,...){
    if(m_write_idx>=WRITE_BUFFER_SIZE)return false;
    va_list ap;
    va_start(ap,fmt);
    int n = vsnprintf(m_write_buf+m_write_idx,WRITE_BUFFER_SIZE-m_write_idx,fmt,ap);
    va_end(ap);
    if(n<0||n>=WRITE_BUFFER_SIZE-m_write_idx)return false;
    m_write_idx+=n;
    return true;
}
bool http_handler::add_options_header(){
    bool ret = true;
    ret &= add_response("Access-Control-Allow-Origin: http://localhost:8080\r\n");
    ret &= add_response("Access-Control-Allow-Methods: GET, POST, DELETE, OPTIONS\r\n");
    ret &= add_response("Access-Control-Allow-Headers: Content-Type\r\n");
    ret &= add_response("Access-Control-Max-Age: 86400\r\n");
    ret &= add_response("Content-Length: 0\r\n");
    ret &= add_response("Connection: close\r\n");
    ret &= add_response("\r\n");
    
    return ret;
}
bool http_handler::add_status_line(int status,const char *title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
} 
bool http_handler::add_headers(int content_len){
    bool ok = add_content_length(content_len)&&add_linger();
    if(!m_extra_headers.empty()){
        ok &= add_response("%s", m_extra_headers.c_str());
        m_extra_headers.clear();
    }
    if(!m_set_cookie.empty()){
        ok &= add_response("%s", m_set_cookie.c_str());
    }
    return ok&&add_blank_line();
}
bool http_handler::add_content_length(int content_len){
    return add_response("Content-Length:%d\r\n",content_len);
}
bool http_handler::add_linger(){
    return add_response("Connection:%s\r\n","keep-alive");
}
bool http_handler::add_blank_line(){
    return add_response("%s","\r\n");
}
bool http_handler::add_content(const char *content){
    return add_response("%s",content);
}
bool http_handler::process_write(){
    HTTP_CODE ret = m_read_ret;
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
    const char* body_wronglogin = "{\"success\":false,\"message\":\"用户名或密码错误，请重新输入\"}";
    const char* body_successlogin = "{\"success\":true,\"message\":\"\"}";
    const char* body_wrongsignup = "{\"success\":false,\"message\":\"用户名或邮箱已存在\"}";
    const char* body_successsignup = "{\"success\":true,\"message\":\"\"}";
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
    else if(ret == OPTIONS){
        add_status_line(200,ok_200);
        
        if(!add_options_header())return false;
    }
    else if(ret == WRONGLOGIN){
        add_status_line(200,ok_200);
        add_headers(strlen(body_wronglogin));
        add_response(body_wronglogin);
    }
    else if(ret == SUCCESSLOGIN){
        add_status_line(200,ok_200);
        add_headers(strlen(body_successlogin));
        add_response(body_successlogin);
    }
    else if(ret == SUCCESSSIGNUP){
        add_status_line(200,ok_200);
        add_headers(strlen(body_successsignup));
        add_response(body_successsignup);
    }
    else if(ret == WRONGSIGNUP){
        add_status_line(200,ok_200);
        add_headers(strlen(body_wrongsignup));
        add_response(body_wrongsignup);
    }
    else if(ret == CREATEPLAN){
        add_status_line(200, ok_200);
        const char* body_create = "{\"success\":true,\"redirectUrl\":\"./create-plan.html\"}";
        add_headers(strlen(body_create));
        add_response(body_create);
    }
    else if(ret == DATA){
        if(!getUserData())return false;
    }
    else if(ret == ADDPLAN){
        if(!addPlan())return false;
    }
    else if(ret == DELETEPLAN){
        if(!deletePlan())return false;
    }
    iov[0].iov_base = m_write_buf;
    iov[0].iov_len = m_write_idx;
    m_iov_count = 1;
    bytes_to_send = m_write_idx;
    return true;
}
bool http_handler::addPlan(){
    std::string planname = m_post_params["planname"];
    std::string content = m_post_params["content"];
    std::string deadline = m_post_params["deadline"];
    std::string status = m_post_params["status"];
    std::string filepth = m_post_params["filepth"];
    if(cur_user.empty()){
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(200,"OK");
        std::string body = "{\"success\":false,\"message\":\"未登录或会话已过期\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        return true;
    }
    if(planname.empty()){
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(400,"Bad Request");
        std::string body = "{\"success\":false,\"message\":\"planname required\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        return true;
    }
    if(status.empty()) status = "unfinished";
    if(!deadline.empty()){
        for(char &ch: deadline){ if(ch=='T') ch = ' '; }
        if(deadline.size()==16) deadline += ":00";
    }
    sql::PreparedStatement* pstmt = nullptr;
    sql::ResultSet* rs = nullptr;
    try{
        if(deadline.empty()){
            std::string sql = "INSERT INTO plan (planname, username, content, status, filepth) VALUES (?,?,?,?,?)";
            pstmt = sqlconn->prepareStatement(sql);
            pstmt->setString(1,planname);
            pstmt->setString(2,cur_user);
            pstmt->setString(3,content);
            pstmt->setString(4,status);
            pstmt->setString(5,filepth);
            pstmt->executeUpdate();
            std::cout<<"11111111";
        }else{
            std::string sql = "INSERT INTO plan (planname, username, content, deadline, status, filepth) VALUES (?,?,?,?,?,?)";
            pstmt = sqlconn->prepareStatement(sql);
            pstmt->setString(1,planname);
            pstmt->setString(2,cur_user);
            pstmt->setString(3,content);
            pstmt->setString(4,deadline);
            pstmt->setString(5,status);
            pstmt->setString(6,filepth);
            pstmt->executeUpdate();
        }
        delete pstmt; pstmt = nullptr;
        pstmt = sqlconn->prepareStatement("SELECT LAST_INSERT_ID() AS id");
        rs = pstmt->executeQuery();
        int new_id = 0;
        if(rs->next()) new_id = rs->getInt("id");
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(200,"OK");
        std::string body = std::string("{\"success\":true,\"planid\":")+std::to_string(new_id)+"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        if(rs) delete rs;
        if(pstmt) delete pstmt;
        std::cout<<"222222222222222222";
        return true;
    }catch(sql::SQLException& e){
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(500,"Internal Error");
        std::string body = std::string("{\"success\":false,\"message\":\"")+e.what()+"\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        if(rs) delete rs;
        if(pstmt) delete pstmt;
        return false;
    }
}
bool http_handler::deletePlan(){
    std::string planid = m_post_params["planid"];
    if(cur_user.empty()){
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(200,"OK");
        std::string body = "{\"success\":false,\"message\":\"未登录或会话已过期\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        return true;
    }
    if(planid.empty()){
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(400,"Bad Request");
        std::string body = "{\"success\":false,\"message\":\"planid required\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        return true;
    }
    sql::PreparedStatement* pstmt = nullptr;
    sql::ResultSet* rs = nullptr;
    try{
        pstmt = sqlconn->prepareStatement("SELECT filepth FROM plan WHERE username=? AND planid=?");
        pstmt->setString(1, cur_user);
        pstmt->setInt(2, std::stoi(planid));
        rs = pstmt->executeQuery();
        std::string filelist;
        if(rs->next()){
            filelist = rs->getString("filepth");
        }
        if(rs){ delete rs; rs = nullptr; }
        if(pstmt){ delete pstmt; pstmt = nullptr; }
        if(!filelist.empty()){
            size_t start = 0;
            while(true){
                size_t pos = filelist.find(';', start);
                std::string f = filelist.substr(start, pos==std::string::npos?std::string::npos:pos-start);
                if(!f.empty()){
                    std::string path = std::string("/home/lzf/wbserver/userdata/")+cur_user+"/"+f;
                    unlink(path.c_str());
                }
                if(pos==std::string::npos) break;
                start = pos+1;
            }
        }
        pstmt = sqlconn->prepareStatement("DELETE FROM plan WHERE username=? AND planid=?");
        pstmt->setString(1, cur_user);
        pstmt->setInt(2, std::stoi(planid));
        int affected = pstmt->executeUpdate();
        if(pstmt){ delete pstmt; pstmt = nullptr; }
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(200,"OK");
        if(affected>0){
            std::string body = "{\"success\":true}";
            add_headers((int)body.size());
            add_response("%s",body.c_str());
        }else{
            std::string body = "{\"success\":false,\"message\":\"记录不存在或无权限\"}";
            add_headers((int)body.size());
            add_response("%s",body.c_str());
        }
        return true;
    }catch(sql::SQLException& e){
        if(rs) delete rs;
        if(pstmt) delete pstmt;
        m_write_idx = 0;
        memset(m_write_buf,0,WRITE_BUFFER_SIZE);
        add_status_line(500,"Internal Error");
        std::string body = std::string("{\"success\":false,\"message\":\"")+e.what()+"\"}";
        add_headers((int)body.size());
        add_response("%s",body.c_str());
        return true;
    }
}
bool http_handler::signUp(){
    std::string input_name, input_passwd, input_email;
    auto it_name = m_post_params.find("username");
    auto it_passwd = m_post_params.find("password");
    auto it_email = m_post_params.find("email");
    if (it_name == m_post_params.end() || it_passwd == m_post_params.end()) {
        std::cout <<"用户名或密码参数缺失！" << std::endl;
        return false;
    }
    input_name = it_name->second;
    input_passwd = it_passwd->second;
    if (it_email != m_post_params.end() && !it_email->second.empty()) {
        input_email = it_email->second;
    } else {
        input_email = input_name + "@placeholder.local";
    }
    sql::PreparedStatement* check_pstmt = nullptr,*signup_pstmt = nullptr;
    sql::ResultSet* check_res = nullptr;

    try {
        std::string check_sql = "SELECT username FROM user WHERE username = ?";
        check_pstmt = sqlconn->prepareStatement(check_sql);
        check_pstmt->setString(1, input_name);
        check_res = check_pstmt->executeQuery();

        if (check_res->next()) {  
                return false;
        } else { 
            std::string signup_sql = "INSERT INTO `user` (username, password, email, create_time, update_time) VALUES (?,SHA2(?, 256),?, NOW(), NOW())";
            signup_pstmt = sqlconn->prepareStatement(signup_sql);
            signup_pstmt->setString(1, input_name);
            signup_pstmt->setString(2, input_passwd);
            signup_pstmt->setString(3, input_email);
            int affected_rows = signup_pstmt->executeUpdate();
            if (affected_rows > 0) {
                std::cout << "用户注册成功！用户名：" << input_name << std::endl;
                return true;
            } else {
                std::cout << "用户注册失败：数据库插入无影响行！" << std::endl;
                return false;
            }
        }
    } catch (sql::SQLException& e) {
        std::cout << "数据库操作异常：" << e.what() << std::endl;
        std::cout << "错误代码：" << e.getErrorCode() << std::endl;
        if (e.getErrorCode() == 1062) {
            return false;
        }
    }
    delete check_res;
    delete check_pstmt;
    delete signup_pstmt;
    return false;
}
bool http_handler::getUserData(){
    sql::PreparedStatement* pstmt_count = nullptr;
    sql::PreparedStatement* pstmt_data = nullptr;
    sql::ResultSet* res_count = nullptr;
    sql::ResultSet* res_data = nullptr;

    try {
        int page = 1;
        int pageSize = 20;
        try {
            if (!m_post_params["page"].empty()) page = std::max(1, std::stoi(m_post_params["page"]));
        } catch (...) { page = 1; }
        try {
            if (!m_post_params["pageSize"].empty()) pageSize = std::max(1, std::stoi(m_post_params["pageSize"]));
        } catch (...) { pageSize = 20; }
        std::string keyword = m_post_params["keyword"];

        std::string sql_count = "SELECT COUNT(*) AS total FROM plan WHERE username=?";
        if (!keyword.empty()) {
            sql_count += " AND (planname LIKE ? OR content LIKE ?)";
        }
        pstmt_count = sqlconn->prepareStatement(sql_count);
        int param_idx = 1;
        pstmt_count->setString(param_idx++, cur_user);
        if (!keyword.empty()) {
            std::string like_key = "%" + keyword + "%";
            pstmt_count->setString(param_idx++, like_key);
            pstmt_count->setString(param_idx++, like_key);
        }
        res_count = pstmt_count->executeQuery();
        int total = 0;
        if (res_count->next()) {
            total = res_count->getInt("total");
        }
        std::string sql_data = "SELECT planid, planname, status, content, deadline, filepth "
                               "FROM plan WHERE username=?";
        if (!keyword.empty()) {
            sql_data += " AND (planname LIKE ? OR content LIKE ?)";
        }
        sql_data += " ORDER BY planid DESC LIMIT ? OFFSET ?";

        pstmt_data = sqlconn->prepareStatement(sql_data);
        param_idx = 1;
        pstmt_data->setString(param_idx++, cur_user);
        if (!keyword.empty()) {
            std::string like_key = "%" + keyword + "%";
            pstmt_data->setString(param_idx++, like_key);
            pstmt_data->setString(param_idx++, like_key);
        }
        int offset = (page - 1) * pageSize;
        pstmt_data->setInt(param_idx++, pageSize);
        pstmt_data->setInt(param_idx++, offset);

        res_data = pstmt_data->executeQuery();

        std::string body;
        body.reserve(256);
        body.append("{\"total\":").append(std::to_string(total)).append(",\"list\":[");

        bool first_item = true;
        while (res_data->next()) {
            if (!first_item) {
                body.append(",");
            }
            first_item = false;

            body.append("{\"planid\":")
                .append(std::to_string(res_data->getInt("planid")))
                .append(",\"planname\":\"")
                .append(res_data->getString("planname"))
                .append("\",\"status\":\"")
                .append(res_data->getString("status"))
                .append("\",\"content\":\"")
                .append(res_data->getString("content"))
                .append("\",\"deadline\":\"")
                .append(res_data->getString("deadline"))
                .append("\",\"filepth\":\"")
                .append(res_data->getString("filepth"))
                .append("\"}");
        }
        body.append("]}");

        m_write_idx = 0;
        memset(m_write_buf, 0, WRITE_BUFFER_SIZE);
        add_status_line(200, "OK");
        add_headers(static_cast<int>(body.size()));
        add_response("%s", body.c_str());

        return true;
    } catch (sql::SQLException& e) {
        std::cerr << "SQL异常：" << e.what() << " (错误码：" << e.getErrorCode() << ")" << std::endl;
        m_write_idx = 0;
        memset(m_write_buf, 0, WRITE_BUFFER_SIZE);
        add_status_line(500, "Internal Error");
        std::string err_body = std::string("{\"code\":500,\"msg\":\"数据库查询失败：") + e.what() + "\",\"total\":0,\"list\":[]}";
        add_headers(static_cast<int>(err_body.size()));
        add_response("%s", err_body.c_str());
        return false;
    } 
    if (res_count != nullptr) delete res_count;
    if (res_data != nullptr) delete res_data;
    if (pstmt_count != nullptr) delete pstmt_count;
    if (pstmt_data != nullptr) delete pstmt_data;
}

bool http_handler::checkLogin(){
    std::string input_name, input_passwd;
    // printf("check login",input_name,input_passwd);
    auto it_name = m_post_params.find("username");
    auto it_passwd = m_post_params.find("password");
    if (it_name == m_post_params.end() || it_passwd == m_post_params.end()) {
        std::cout <<"用户名或密码参数缺失！" << std::endl;
        return false;
    }
    input_name = it_name->second;
    input_passwd = it_passwd->second;
    sql::PreparedStatement* pstmt = nullptr;
    sql::ResultSet* res = nullptr;

    try {
        std::string sql = "SELECT password FROM user WHERE username = ?";
        pstmt = sqlconn->prepareStatement(sql);
        pstmt->setString(1, input_name);
        res = pstmt->executeQuery();

        if (res->next()) {  
            std::string db_passwd = res->getString("password");
            unsigned char hash[SHA256_DIGEST_LENGTH];
            // 对明文密码计算SHA256哈希（二进制）
            SHA256((const unsigned char*)input_passwd.c_str(), input_passwd.length(), hash);
            std::stringstream ss;
            for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
            }
            std::cout<<db_passwd <<std::endl<<ss.str();
            if (db_passwd == ss.str()) {
                cur_user = input_name;
                {
                    std::string sid = gen_sid();
                    long long exp = now_sec() + 7LL*24*3600;
                    {
                        std::lock_guard<std::mutex> lk(g_sess_mtx);
                        g_sessions[sid] = {cur_user, exp};
                    }
                    m_set_cookie = std::string("Set-Cookie: session=")+sid+"; Path=/; HttpOnly; SameSite=Lax; Max-Age=604800\r\n";
                }
                return true;
            } else {
                std::cout << "密码错误！" << std::endl;
                return false;
            }
        } else { 
            std::cout << "用户名不存在！" << std::endl;
            return false;
        }
    } catch (sql::SQLException& e) {
        std::cout << "数据库操作异常：" << e.what() << std::endl;
        std::cout << "错误代码：" << e.getErrorCode() << std::endl;
    }
    delete res;
    delete pstmt;
    return false;
}

bool http_handler::write(){
    int ret = 0;
    if(bytes_to_send==0){
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
            iov[0].iov_len = iov[0].iov_len-ret;
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
    if(m_tmp_fd!=-1){
        close(m_tmp_fd);
        m_tmp_fd = -1;
    }
    if(!m_tmp_path.empty()){
        unlink(m_tmp_path.c_str());
        m_tmp_path.clear();
    }
    close_map();
    epoll_ctl(m_epollfd,EPOLL_CTL_DEL,m_clifd,0);
    close(m_clifd);
    init();
}
bool http_handler::process(){
    if(!read_once()){
        printf("read error");
        return false;
    }
    m_read_ret = process_read();
    if(m_read_ret==NO_REQUEST){
        epoll_event ev{};
        ev.events = EPOLLIN|EPOLLET;
        ev.data.fd = m_clifd;
        epoll_ctl(m_epollfd,EPOLL_CTL_MOD,m_clifd,&ev);
        return true;
    }
    if(!process_write()){
        return false;
    }
    if(!write()){
        return false;
    }

    return true;
}
//     
//     if(!write())return false;
//     return true;
// }
