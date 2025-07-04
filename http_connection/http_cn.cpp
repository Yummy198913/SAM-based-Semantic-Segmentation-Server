#include"http_cn.h"

// 定义HTTP响应的一些状态信息
const char* ok_200_title = "OK";
const char* error_400_title = "Bad Request";
const char* error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char* error_403_title = "Forbidden";
const char* error_403_form = "You do not have permission to get file from this server.\n";
const char* error_404_title = "Not Found";
const char* error_404_form = "The requested file was not found on this server.\n";
const char* error_500_title = "Internal Error";
const char* error_500_form = "There was an unusual problem serving the requested file.\n";

int http_connection::m_epoll_fd = -1;
int http_connection::m_user_count = 0;

const char * doc_root = "/raid/zhangyaming2/WebServer/resources";

//设置fd 非阻塞
void setnonblocking(int fd)
{
    int old_flag = fcntl(fd, F_GETFL);
    int new_flag = old_flag | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_flag);
}

//把fd添加到epoll中
void add_fd(int epoll_fd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    // event.events = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT; // EPOLLRDHUP: 对对方链接断开，可以去处理，之前是根据read返回值判断
    // event.events = EPOLLIN | EPOLLRDHUP; // EPOLLRDHUP: 对对方链接断开，可以去处理，之前是根据read返回值判断
    event.events = EPOLLIN | EPOLLRDHUP;
    if (one_shot)
    {
        event.events | EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}


//从epoll中删除fd
void remove_fd(int epoll_fd, int fd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

//修改文件描述符, 重置socket上的one_sot事件，确保下一次可读时， EPOLLIN事件能被触发
void modify_fd(int epoll_fd, int fd, int ev){
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLONESHOT | EPOLLRDHUP; // 修改了要重新注册
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
    //  设置文件描述符非阻塞,因为用的是边沿触发
    setnonblocking(fd);
}


http_connection::http_connection(){

}

http_connection::~http_connection()
{
    
}

// 关闭连接
void http_connection::close_conn() {
    if(m_socket_fd != -1) {
        remove_fd(m_epoll_fd, m_socket_fd);
        m_socket_fd = -1;
        m_user_count--; // 关闭一个连接，将客户总数量-1
    }
}

void http_connection::init(int client_fd, const sockaddr_in &client_addr)
{
    m_socket_fd = client_fd;
    addr = client_addr;

    //端口复用
    int reuse = 1;
    setsockopt(m_socket_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    //添加到epoll对象中
    //手动更新fd到自己定义的event中
    add_fd(m_epoll_fd, m_socket_fd, true);
    m_user_count++;
    init();
}

void http_connection::init(){
    m_check_state = CHECK_STATE_REQUESTLINE;    // 初始状态为检查请求行
    m_linger = false;       // 默认不保持链接  Connection : keep-alive保持连接

    m_method = GET;         // 默认请求方式为GET
    m_url = 0;              
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_index = 0;
    m_read_index = 0;
    m_write_idx = 0;
    
    m_is_receiving_image = false;
    m_mody_first_read = false;
    m_image_start_index = 0;
    m_body_start_index = 0;

    bzero(m_read_buff, READ_BUFFER_SIZE);
    bzero(m_write_buf, READ_BUFFER_SIZE);
    bzero(m_real_file, FILENAME_LEN);
}


bool http_connection::read()
{
    // 已经读取到的字节
    int bytes_read = 0;
    while(1)
    {
        printf("read(): 当前读取了 %d 字节，总共读取到：%d 字节\n", bytes_read, m_read_index);
        //第二个参数是buff的内存地址，从哪个位置开始读，读多少个字节，READ_BUFFER_SIZE - m_read_index表示读完缓冲区的内容
        bytes_read = recv(m_socket_fd, m_read_buff + m_read_index, READ_BUFFER_SIZE - m_read_index, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                //读完了
                // printf("errno == EAGAIN || errno == EWOULDBLOCK\n");
                printf("读取完毕\n");
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            //对方该关闭链接
            printf("bytes_read == 0\n");
            return false;
        }
        m_read_index += bytes_read;
        printf("读取到了数据： %s\n", m_read_buff);

        if (m_read_index >= READ_BUFFER_SIZE)
        {
            return true;
        }

    }
    return true;
}

http_connection::HTTP_CODE http_connection::process_read(){ 
    //定义初始的状态
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char * text = 0;
    printf("等待进入解析循环\n");
    while(1)
    {
        if (m_check_state == CHECK_STATE_CONTENT)
        {
            if (m_body_buffer.empty() && !m_mody_first_read)
            {
                //第一次开始读请求体
                m_body_start_index = m_start_line;
                m_mody_first_read = true;
            }
            printf("------开始解析请求体------\n");

            int content_read = m_read_index - m_body_start_index;
            printf("请求体： m_read_index: %d, m_body_start_index: %d, content_read: %d\n", m_read_index, m_body_start_index, content_read);

            if (content_read > 0) {
                m_body_buffer.insert(m_body_buffer.end(),
                            m_read_buff + m_body_start_index,
                            m_read_buff + m_read_index);
                m_body_start_index = m_read_index; // 更新 start_line 避免重复读入
                printf("插入 %d 字节到请求体，当前总大小：%d\n", content_read, (int)m_body_buffer.size());
            }

            if (m_read_index >= READ_BUFFER_SIZE){ // 重置
                m_read_index = 0;
                m_body_start_index = 0;
            }

            if ((int)m_body_buffer.size() < m_content_length) {
                printf("请求体未读完（当前：%d / 预期：%d）\n", (int)m_body_buffer.size(), m_content_length);
                return NO_REQUEST; // 请求体未读完
            }

            // body 全部读完后再解析
            ret = parse_request_content_body(m_body_buffer.data(), m_body_buffer.size());
            if (ret == GET_REQUEST) {
                return do_request_img();
            }
            printf("------请求体解析完毕------\n");
            return ret;
        }
        else if (m_check_state == CHECK_STATE_REQUESTLINE)
        {
            line_status = parse_line();
            if (line_status == LINE_OK)
            {
                text = get_line();
                m_start_line = m_checked_index;
                printf("获取一行HTTP信息： %s\n", text);

                ret = parse_request_line( text );
                if ( ret == BAD_REQUEST ) {
                    return BAD_REQUEST;
                }
            }
            else if (line_status == LINE_OPEN)
            {
                break; // 没读完行，等待更多数据
            }
            else{
                return BAD_REQUEST;
            }
        }
        else if (m_check_state == CHECK_STATE_HEADER)
        {
            printf("------解析请求头------\n");
            line_status = parse_line();
            if (line_status == LINE_OK)
            {
                text = get_line();
                m_start_line = m_checked_index;
                printf("获取一行HTTP信息： %s\n", text);

                ret = parse_request_head(text);
                printf("头解析结果： %d\n", ret);
                if (ret == BAD_REQUEST)
                {
                    return BAD_REQUEST;
                }
                else if (ret == GET_REQUEST)
                {
                    return do_request(); // 去解析具体的内容
                }
            }
            else if (line_status == LINE_OPEN)
            {
                break; // 没读完行，等待更多数据
            }
            else{
                return BAD_REQUEST;
            }
        }
        else {
            return BAD_REQUEST;
        }
    }
}

bool http_connection::is_buffer_full()
{
    return (m_checked_index == READ_BUFFER_SIZE);
}

// 写HTTP响应
bool http_connection::write()
{
    // 如果是第一次发送，初始化发送状态
    if (!m_is_sending) {
        m_bytes_sent = 0;
        m_is_sending = true;
        
        // 设置分散写的缓冲区
        if (m_iv_count == 2) {
            // 同时发送响应头和文件内容
            m_total_bytes = m_write_idx + m_file_stat.st_size;
        } else {
            // 只发送响应头
            m_total_bytes = m_write_idx;
        }
    }

    int temp = 0;
    size_t bytes_to_send = m_total_bytes - m_bytes_sent;
    
    if (bytes_to_send <= 0) {
        // 所有数据已发送完毕
        m_is_sending = false;
        unmmap();
        
        if(m_linger) {
            // 保持连接，重置为读事件
            init();
            modify_fd(m_epoll_fd, m_socket_fd, EPOLLIN);
        } else {
            // 关闭连接
            close_conn();
        }
        
        return true;
    }

    // 重新设置分散写的缓冲区，考虑已发送的字节数
    struct iovec iov[2];
    int iov_count = 0;

    // 第一部分：HTTP响应头
    if (m_bytes_sent < m_write_idx) {
        iov[iov_count].iov_base = m_write_buf + m_bytes_sent;
        iov[iov_count].iov_len = m_write_idx - m_bytes_sent;
        iov_count++;
    }

    // 第二部分：文件内容
    if (m_iv_count == 2 && m_bytes_sent >= m_write_idx) {
        size_t file_offset = m_bytes_sent - m_write_idx;
        iov[iov_count].iov_base = (char*)m_file_address + file_offset;
        iov[iov_count].iov_len = m_file_stat.st_size - file_offset;
        iov_count++;
    }

    // 执行分散写
    while (bytes_to_send > 0) {
        temp = writev(m_socket_fd, iov, iov_count);
        
        if (temp <= -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 缓冲区满，等待下一次写事件
                printf("TCP发送缓冲区已满，暂停发送，已发送 %zu 字节，剩余 %zu 字节\n", 
                       m_bytes_sent, bytes_to_send);
                modify_fd(m_epoll_fd, m_socket_fd, EPOLLOUT);
                return true;
            }
            
            // 其他错误，关闭连接
            printf("写操作失败，错误码: %d (%s)\n", errno, strerror(errno));
            unmmap();
            return false;
        }
        
        // 更新已发送的字节数
        m_bytes_sent += temp;
        bytes_to_send -= temp;
        
        // 如果所有数据都已发送，退出循环
        if (bytes_to_send <= 0) {
            break;
        }
        
        // 重新调整缓冲区（如果需要）
        iov_count = 0;
        
        if (m_bytes_sent < m_write_idx) {
            iov[iov_count].iov_base = m_write_buf + m_bytes_sent;
            iov[iov_count].iov_len = m_write_idx - m_bytes_sent;
            iov_count++;
        }
        
        if (m_iv_count == 2 && m_bytes_sent >= m_write_idx) {
            size_t file_offset = m_bytes_sent - m_write_idx;
            iov[iov_count].iov_base = (char*)m_file_address + file_offset;
            iov[iov_count].iov_len = m_file_stat.st_size - file_offset;
            iov_count++;
        }
    }

    // 如果还有数据需要发送，继续监听写事件
    if (m_bytes_sent < m_total_bytes) {
        modify_fd(m_epoll_fd, m_socket_fd, EPOLLOUT);
        return true;
    }
    
    // 所有数据已发送完毕
    return write(); // 递归调用以处理可能的清理工作
}

// // 写HTTP响应
// bool http_connection::write()
// {
//     int temp = 0;
//     int bytes_have_send = 0;    // 已经发送的字节
//     int bytes_to_send = m_write_idx;  // 将要发送的字节 （m_write_idx）写缓冲区中待发送的字节数
    
//     if ( bytes_to_send == 0 ) {
//         // 将要发送的字节为0，这一次响应结束。
//         modify_fd( m_epoll_fd, m_socket_fd, EPOLLIN ); 
//         init();
//         return true;
//     }

//     while(1) {
//         // 分散写
//         temp = writev(m_socket_fd, m_iv, m_iv_count);
//         if ( temp <= -1 ) {
//             // 如果TCP写缓冲没有空间，则等待下一轮EPOLLOUT事件，虽然在此期间，
//             // 服务器无法立即接收到同一客户的下一个请求，但可以保证连接的完整性。
//             if( errno == EAGAIN || errno == EWOULDBLOCK ) {
//                 printf("写操作暂时不可用，错误码: %d (EAGAIN)\n", errno);
//                 modify_fd( m_epoll_fd, m_socket_fd, EPOLLOUT );
//                 return true;
//             }
//             unmmap();
//             return false;
//         } else if (errno == EPIPE)
//         {
//             printf("客户端断开连接\n");
//             unmmap();
//             return false;
//         } else {
//             printf("写操作失败，错误码: %d (%s)\n", errno, strerror(errno));
//             unmmap();
//             return false;
//         }

//         bytes_to_send -= temp;
//         bytes_have_send += temp;

//         if ( bytes_to_send <= bytes_have_send ) {
//             // 发送HTTP响应成功，根据HTTP请求中的Connection字段决定是否立即关闭连接
//             unmmap();
//             if(m_linger) {
//                 init();
//                 modify_fd( m_epoll_fd, m_socket_fd, EPOLLIN );
//                 return true;
//             } else {
//                 modify_fd( m_epoll_fd, m_socket_fd, EPOLLIN );
//                 return false;
//             } 
//         }
//     }
// }

// 往写缓冲中写入待发送的数据
bool http_connection::add_response( const char* format, ... ) {
    if( m_write_idx >= WRITE_BUFFER_SIZE ) {
        return false;
    }
    va_list arg_list;
    va_start( arg_list, format );
    int len = vsnprintf( m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list );
    if( len >= ( WRITE_BUFFER_SIZE - 1 - m_write_idx ) ) {
        return false;
    }
    m_write_idx += len;
    va_end( arg_list );
    return true;
}

bool http_connection::add_status_line( int status, const char* title ) {
    return add_response( "%s %d %s\r\n", "HTTP/1.1", status, title );
}

bool http_connection::add_headers(int content_len) {
    return add_content_length(content_len) &
    add_content_type("text/html") &
    add_linger() &
    add_blank_line();
}

bool http_connection::add_content_length(int content_len) {
    return add_response( "Content-Length: %d\r\n", content_len );
}

bool http_connection::add_linger()
{
    return add_response( "Connection: %s\r\n", ( m_linger == true ) ? "keep-alive" : "close" );
}

bool http_connection::add_blank_line()
{
    return add_response( "%s", "\r\n" );
}

bool http_connection::add_content( const char* content )
{
    return add_response( "%s", content );
}

bool http_connection::add_content_type(const char* type) {
    // return add_response("Content-Type:%s\r\n", "text/html");  // 根据不同的请求返回不同的content type
    return add_response("Content-Type:%s\r\n", type);
}

// 根据服务器处理HTTP请求的结果，决定返回给客户端的内容
bool http_connection::process_write(HTTP_CODE ret) {
    
    switch (ret)
    {
        case INTERNAL_ERROR:
            add_status_line( 500, error_500_title );
            add_headers( strlen( error_500_form ) );
            if ( ! add_content( error_500_form ) ) {
                return false;
            }
            break;
        case BAD_REQUEST:
            add_status_line( 400, error_400_title );
            add_headers( strlen( error_400_form ) );
            if ( ! add_content( error_400_form ) ) {
                return false;
            }
            break;
        case NO_RESOURCE:
            add_status_line( 404, error_404_title );
            add_headers( strlen( error_404_form ) );
            if ( ! add_content( error_404_form ) ) {
                return false;
            }
            break;
        case FORBIDDEN_REQUEST:
            add_status_line( 403, error_403_title);
            add_headers(strlen( error_403_form));
            if ( ! add_content( error_403_form)) {
                return false;
            }
            break;

        case FILE_REQUEST:
            add_status_line(200, ok_200_title);
            add_headers(m_file_stat.st_size);
            m_iv_count = 2;
            m_iv[ 0 ].iov_base = m_write_buf;
            m_iv[ 0 ].iov_len = m_write_idx;
            m_iv[ 1 ].iov_base = m_file_address;
            m_iv[ 1 ].iov_len = m_file_stat.st_size;
            printf("FILE_REQUEST 处理完毕\n");
            return true;

        case IMG_REQUEST:
        {
            printf("m_real_file: %s\n", m_real_file);
            add_status_line(200, ok_200_title);
            const char * ext = strrchr(m_real_file, '.'); // 查找一个字符在字符串中的最后一个位置
            if (ext){
                if (strcasecmp(ext, ".jpg") == 0 || strcasecmp(ext, ".jpeg") == 0) {
                    add_content_type("image/jpeg");
                } 
                else if (strcasecmp(ext, ".png") == 0) {
                    add_content_type("image/png");
                } 
                else if (strcasecmp(ext, ".bmp") == 0) {
                    add_content_type("image/bmp");
                } 
                else if (strcasecmp(ext, ".gif") == 0) {
                    add_content_type("image/gif");
                } 
                else {
                    add_content_type("application/octet-stream"); // 默认
                }
            } 
            else {
                    add_content_type("application/octet-stream"); // 没有后缀
                }
            //添加头
            add_content_length(m_file_stat.st_size);
            add_linger();
            add_blank_line();
            m_iv_count = 2;
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            printf("IMG_REQUEST 处理完毕，准备发送 %zu 字节\n", m_write_idx + m_file_stat.st_size);
            return true;
        }

        default:
            return false;
    }

    m_iv[ 0 ].iov_base = m_write_buf;
    m_iv[ 0 ].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}


// 解析http请求行，获得请求方法，目标是url，http版本
http_connection::HTTP_CODE http_connection::parse_request_line(char * text){
    //GET /upload?click=100,200 HTTP/1.1
    m_url = strpbrk(text, " \t"); // 指向 "/upload?click=100,200 HTTP/1.1"
    
    if (!m_url) return BAD_REQUEST;
    *m_url++ = '\0'; // 截断method，m_url指向 "/upload?click=100,200 HTTP/1.1"

    char * method = text; // "GET" 或 "POST"
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else if (strcasecmp(method, "POST") == 0) {
        m_method = POST;
    } else {
        return BAD_REQUEST;
    }

    // 保存原始指针和HTTP版本位置
    char* original_url = m_url; // 指向 "/upload?click=100,200 HTTP/1.1"
    char* version_pos = strpbrk(m_url, " \t"); // 指向 " HTTP/1.1"
    
    if (!version_pos) return BAD_REQUEST;
    
    // 查找参数起始位置 ?
    char* query_start = strchr(m_url, '?');
    
    if (query_start && query_start < version_pos) { // 确保?在HTTP版本之前
        // 临时截断URL以解析参数（不影响原始数据）
        char original_char = *query_start;
        *query_start = '\0'; // 临时截断为 "/upload"
        
        // 解析参数 "click=100,200 HTTP/1.1"
        char* params = query_start + 1; // 指向 "click=100,200 HTTP/1.1"
        
        // 查找click参数
        char* click_param = strstr(params, "click=");
        if (click_param) {
            click_param += 6; // 指向 "100,200 HTTP/1.1"
            
            // 查找逗号分隔x和y
            char* comma = strchr(click_param, ',');
            if (comma) {
                *comma = '\0'; // 临时截断为 "100"
                m_click_x = atoi(click_param);
                
                // 恢复并处理y值 "200 HTTP/1.1"
                *comma = ','; // 恢复逗号
                char* y_start = comma + 1; // 指向 "200 HTTP/1.1"
                
                // 截断HTTP版本（如果存在）
                char* space_after_y = strchr(y_start, ' ');
                if (space_after_y) *space_after_y = '\0'; // 临时截断为 "200"
                
                m_click_y = atoi(y_start);
                
                // 恢复HTTP版本分隔符
                if (space_after_y) *space_after_y = ' ';
                
                printf("从URL解析到坐标：click_x=%d, click_y=%d\n", m_click_x, m_click_y);
            } else {
                printf("click参数格式错误：缺少逗号\n");
                return BAD_REQUEST;
            }
        } else {
            printf("URL中未找到click参数\n");
        }
        
        // 恢复原始字符串
        *query_start = original_char;
    } else {
        printf("URL中无查询参数\n");
    }

    // 恢复m_url为不含参数但包含HTTP版本的原始格式
    // 例如：从 "/upload?click=100,200 HTTP/1.1" 恢复为 "/upload HTTP/1.1"
    if (query_start && query_start < version_pos) {
        // 移动参数部分后的所有内容（包括HTTP版本）
        memmove(query_start, version_pos, strlen(version_pos) + 1);
    }

    // 重新查找HTTP版本位置（因为字符串可能已被移动）
    m_version = strpbrk(m_url, " \t");
    if (!m_version) return BAD_REQUEST;
    
    *m_version++ = '\0'; // 截断为 "/upload"，m_version指向 "HTTP/1.1"

    // 验证HTTP版本
    if (strncasecmp(m_version, "HTTP/", 5) != 0) {
        return BAD_REQUEST;
    }

    // 处理可能的协议前缀（http:// 或 https://）
    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        if (!(m_url = strchr(m_url, '/'))) return BAD_REQUEST;
    } else if (strncasecmp(m_url, "https://", 8) == 0) {
        m_url += 8;
        if (!(m_url = strchr(m_url, '/'))) return BAD_REQUEST;
    }

    // 验证路径有效性
    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

http_connection::HTTP_CODE http_connection::parse_request_head(char * text){
    //遇到空行，表示头部字段解析完毕
    if (text[0] == '\0')
    {
        //如果HTTP请求有消息体，则还需要读取m_content_length字节的消息体
        //状态机转移到check_state_content状态
        if (m_content_length != 0)
        {
            printf("请求体长度: %d, 继续解析请求体\n", m_content_length);
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST; // 解析不完整
        }
        //否则说明我们已经得到了一个完整的HTTP请求

        if (m_method == POST)
        {
            printf("---POST：解析请求头完毕，开始解析请求体...\n");
            return NO_REQUEST; // 如果是POST方法，还要去解析请求体
        }
        else {
            return GET_REQUEST; // 如果是GET，就返回，因为没有请求体，解析完毕了。
        }
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        //处理connection头部字段  Connection: keep-alive
        text +=11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0){
            m_linger = true; // 要保持链接
        }
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text+= strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Content-Type:", 13) == 0)
    {
        text+=13;
        text+= strspn(text, " \t");
        if(strncasecmp(text, "multipart/form-data", 19) == 0)
        {
            printf("检查到图像：%s\n", text);
            char* boundary_str = strstr(text, "boundary=") + 9;
            m_boundary = std::string("--") + boundary_str;  // ------WebKitFormBoundaryzkBuSHU4YBmK2a9u
            printf("边界： %s\n", m_boundary.c_str());
        }
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    }
    else {
        printf("未知header:  %s\n", text);
    }
    return NO_REQUEST;
}

http_connection::HTTP_CODE http_connection::parse_request_content_body(const char* body, int length) {
    printf("---开始解析请求体---\n");
    // 查找 boundary 起始位置
    const char* start = (const char *)memmem(body, length, m_boundary.c_str(), m_boundary.size());
    if (!start){
        printf("未找到边界起始位置\n");
        return BAD_REQUEST;
    }
    printf("找到边界起始位置: %s\n", start);

    // 查找 Content-Disposition
    const char* disp = (const char *)memmem(start, length, "Content-Disposition", std::string("Content-Disposition").size());
    if (!disp){
        printf("未找到Content-Disposition\n");
        return BAD_REQUEST;
    }
    printf("找到Content-Disposition: %s\n", disp);

    // 判断 name=xxx
        const char* name_pos = strstr(disp, "name=\"");
        name_pos += 6;
        const char* name_end = strchr(name_pos, '"');
        std::string field_name(name_pos, name_end - name_pos);

    // 查找 Content-Type
    const char* ctype = (const char *)memmem(disp, length, "Content-Type", std::string("Content-Type").size());
    if (!ctype)
    {
        printf("未找到Content-Type\n");
        return BAD_REQUEST;
    }
    printf("找到类型: %s\n", ctype);

    // 跳过一个空行，到达真正的文件内容
    const char* data_start = (const char *) memmem(ctype, length, "\r\n\r\n", std::string("\r\n\r\n").size());
    if (!data_start){
        printf("未找到空行\n");
        return BAD_REQUEST;
    }
    data_start += 4; // 跳过 \r\n\r\n

    // 5. 使用 content-length 推断图像长度
    int header_offset = data_start - body;
    int image_len = m_content_length - header_offset;

    if (image_len <= 0 || header_offset + image_len > length) {
        printf("图像长度非法：image_len = %d\n", image_len);
        return BAD_REQUEST;
    }

    printf("图像偏移量 = %d, 图像长度 = %d\n", header_offset, image_len);

    std::string receive_path = "/raid/zhangyaming2/WebServer/receive_imgs/temp.jpg";

    FILE* fp = fopen(receive_path.c_str(), "wb");
    if (!fp) return INTERNAL_ERROR;
    fwrite(data_start, 1, image_len, fp);
    fclose(fp);

    printf("图像数据写入完成\n");
    return GET_REQUEST;
}

http_connection::HTTP_CODE http_connection::do_request_img()
{
    if (m_method != POST) return BAD_REQUEST;
    // seg result
    Seg(m_click_x, m_click_y);

    const char* path = "/raid/zhangyaming2/WebServer/segment-anything-main/inference_res/seg_out.jpg";

    strcpy(m_real_file, path); // 赋值给m_real_file

    if(stat(m_real_file, &m_file_stat) < 0)
    {
        printf("发送结果：NO_REQUEST\n");
        return NO_REQUEST;
    }

    // if (!(m_file_stat.st_mode & S_IROTH))
    // {
    //     return FORBIDDEN_REQUEST;
    // }

    if (S_ISDIR(m_file_stat.st_mode))
    {
        printf("发送结果：BAD_REQUEST\n");
        return BAD_REQUEST;
    }

    //映射fd
    int fd = open(m_real_file, O_RDONLY); // 只读打开文件
    m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    if (m_file_address == MAP_FAILED)
    {
        printf("mmap 失败: %s\n", m_real_file);
        m_file_address = 0;
        return INTERNAL_ERROR;
    }

    close(fd);

    printf("do_request_img: 成功映射文件，返回IMG_REQUEST\n");
    return IMG_REQUEST;
}


http_connection::HTTP_CODE http_connection::do_request()
{
    // "/home/xxxx/resources"
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    //获取m_real_file文件的相关状态信息，-1失败，0成功
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }

    // 判断访问权限
    if (! (m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }

    //判断是否为目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }

    //以只读方式打开文件
    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *) mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void http_connection::Seg(int x, int y)
{
    std::string cmd = "/raid/zhangyaming2/miniconda3/envs/sam/bin/python ./segment-anything-main/inference_img.py --point_coords " + std::to_string(x) + " " + std::to_string(y);
    printf("执行命令: %s\n", cmd.c_str());
    int result = system(cmd.c_str());
    if (result != 0) {
        printf("Python脚本执行失败，返回码: %d\n", result);
    }
}

//对内存映射区操作
void http_connection::unmmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

http_connection::LINE_STATUS http_connection::parse_line(){
    //解析一行 判断依据是\n
    char temp;

    for(; m_checked_index < m_read_index; ++m_checked_index)
    {
        temp = m_read_buff[m_checked_index];
        if (temp == '\r')
        {
            if ((m_checked_index + 1) == m_read_index)  // ?
            {
                return LINE_OPEN;
            }
            else if (m_read_buff[m_checked_index + 1] == '\n')
            {
                m_read_buff[m_checked_index++] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_index > 1) && (m_read_buff[m_checked_index-1] == '\r'))
            {
                m_read_buff[m_checked_index-1] = '\0';
                m_read_buff[m_checked_index++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }
    return LINE_OPEN;

}


// 线程池中的工作线程调用，处理HTTP的入口函数
void http_connection::process()
{
    // 解析http请求
    HTTP_CODE read_ret = process_read();

    if( read_ret == NO_REQUEST)
    {
        if (m_check_state == CHECK_STATE_CONTENT)
        {
            // 当前 body 未读完，等待下一次 read() 接收后再继续处理
            printf("请求体未完成（%d / %d），继续等待...\n", (int)m_body_buffer.size(), m_content_length);
        }
        modify_fd(m_epoll_fd, m_socket_fd, EPOLLIN); // 请求不完整，需要继续读取客户数据
        return;
    }

    printf("解析到完整的请求\n");
    printf("读取到的结果： %d\n", read_ret);
    printf("进入写响应\n");

    //生成响应
    bool write_ret = process_write(read_ret);

    printf("发送结果： %d\n", write_ret);
    
    modify_fd(m_epoll_fd, m_socket_fd, EPOLLOUT); // 因为添加了oneshot事件
}
