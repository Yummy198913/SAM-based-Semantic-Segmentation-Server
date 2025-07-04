#ifndef HTTP_CONNECT_H
#define HTTP_CONNECT_H

#include<sys/epoll.h>
#include<signal.h>
#include<fcntl.h>
#include<unistd.h>
#include<arpa/inet.h>
#include<sys/stat.h>
#include<sys/mman.h>
#include<stdarg.h>
#include<errno.h>
#include"../locker/locker.h"
#include<sys/uio.h>
#include<cstdio>
#include<string.h>
#include<stdlib.h>
#include<string.h>
#include<string>
#include<vector>

class http_connection{

public:
    static int m_epoll_fd; // 所有的socket的事件都注册到一个epoll_fd,是全局的，所以要定义static
    static int m_user_count; // 统计用户的数量
    static const int READ_BUFFER_SIZE = 65535; // 缓冲区的大小
    static const int WRITE_BUFFER_SIZE = 2048;// 缓冲区的大小
    static const int FILENAME_LEN = 200;      // 文件名的最大长度

    //定义状态
    // HTTP请求方法，这里只支持GET
    enum METHOD {GET = 0, POST, HEAD, PUT, DELETE, TRACE, OPTIONS, CONNECT};
    
    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };
    
    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        IMG_REQUEST         :   图像请求，获取图像成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE {NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, 
    FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION,
    IMG_REQUEST};
    
    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    http_connection();
    ~http_connection();

    void init(int client_fd, const sockaddr_in &client_addr); // 当有客户连接进来了之后，初始化该用户信息
    void close_conn(); // 关闭客户端链接
    void process(); // 处理客户端的请求：解析请求报文，找到资源，封装成响应信息

    bool read(); // 非阻塞读
    bool write(); // 非阻塞写

    void init(); // 初始化链接其余的数据
    HTTP_CODE process_read(); // 解析http请求
    HTTP_CODE parse_request_line(char * text); // 解析请求首行
    HTTP_CODE parse_request_head(char * text); // 解析请求头
    HTTP_CODE parse_request_content(char * text); // 解析请求体
    HTTP_CODE parse_request_content_body(const char* body, int length); // 一次性解析所有body

    LINE_STATUS parse_line(); // 解析具体一行

    char * get_line() {return m_read_buff + m_start_line;} // 获取一行数据
    HTTP_CODE do_request();
    HTTP_CODE do_request_img(); // 返回分割结果
    void unmmap(); // 释放映射
    bool is_buffer_full(); // 缓冲区是否满了

    void Seg(int x, int y); //

private:

    int m_socket_fd; //该http链接的socket
    sockaddr_in addr; // 通信的socket地址
    char m_read_buff[READ_BUFFER_SIZE]; // 读缓冲区，数组
    int m_read_index; // 标识度缓冲区已经读入的数据的最后一个字节的下一个位置（从哪个位置开始读）
    bool process_write( HTTP_CODE ret );    // 填充HTTP应答

    int m_checked_index; // 当前正在分析的字符在读缓冲区的位置
    int m_start_line; //当前正在解析的行的起始位置
    bool m_mody_first_read;
    int m_body_start_index; // 读取请求体的时候，读取到的长度
    int m_content_length; // 读取到的content的长度
    CHECK_STATE m_check_state; //主状态机的状态

    char * m_url; // 请求目标文件的文件名
    char * m_version; // 协议版本，只支持http1.1
    METHOD m_method; // 请求方法。
    char * m_host; // 主机名
    bool  m_linger; // http是否要请保持连接

    char m_write_buf[ WRITE_BUFFER_SIZE ];  // 写缓冲区
    int m_write_idx;                        // 写缓冲区中待发送的字节数
    char m_real_file[200]; // 客户请求的目标文件的完整路径，其内容等于doc_root + m_url, doc_root 是网站根目录
    char* m_file_address;  // 客户请求的目标文件被mmap到内存中的起始位置
    struct stat m_file_stat; // 目标文件的状态。通过它我们可以判断文件是否存在、是否为目录、是否可读，并获取文件大小等信息
    struct iovec m_iv[2];    // 我们将采用writev来执行写操作，所以定义下面两个成员，其中m_iv_count表示被写内存块的数量。
    int m_iv_count;
    bool m_is_receiving_image; // 当前处理的是否是二进制图像数据
    int m_image_start_index; // 图像数据开始index
    std::vector<char> m_body_buffer; // 请求体存放数组

    std::string m_boundary; // 上传过来的图像的唯一标记

    // 发送状态变量 for 图像
    size_t m_bytes_sent;        // 已发送的总字节数
    size_t m_total_bytes;       // 需要发送的总字节数
    bool m_is_sending;          // 是否正在发送数据

    // 用户点击的坐标
    int m_click_x;
    int m_click_y;

    bool add_response( const char* format, ... );
    bool add_content( const char* content );
    bool add_content_type(const char* type);
    bool add_status_line( int status, const char* title );
    bool add_headers( int content_length );
    bool add_content_length( int content_length );
    bool add_linger();
    bool add_blank_line();
    
};

#endif