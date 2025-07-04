#include<iostream>
#include<cstdio>
#include<string.h>
#include<sys/socket.h>
#include<arpa/inet.h>
#include<errno.h>
#include<sys/epoll.h>
#include<netinet/in.h>
#include"locker/locker.h"
#include"threadpool/threadpool.h"
#include<fcntl.h>
#include<signal.h>
#include"http_connection/http_cn.h"

#define MAX_FD 65535 // 最大文件描述符个数
#define MAX_EVENTS 10000 // 一次监听最大事件数组

extern void add_fd(int epoll_fd, int fd, bool one_shot);
extern void remove_fd(int epoll_fd, int fd);
extern void modify_fd(int epoll_fd, int fd, int ev);

//对信号处理，添加信号
void addsig(int sig, void (handler)(int))
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    sigaction(sig, &sa, NULL);
}

int main(int argc, char *argv[])
{
    if (argc <=1)
    {
        printf("do as the following format: %s port number\n", basename(argv[0])); // 提取文件名

        return 1;
    }

    int port = atoi(argv[1]);

    //对SIGPIPE信号作出处理（它的主要作用是通知进程尝试向一个已经关闭的管道或套接字写入数据时发生错误。默认终止进程）
    addsig(SIGPIPE, SIG_IGN);

    //创建线程池,任务类型是http链接
    Threadpool<http_connection> * pool = NULL;
    try{
        pool = new Threadpool<http_connection>;
    } catch (...)
    {
        return 1;
    }


    //创建一个数组保存所有用户的信息,也放到http_connect类里面
    http_connection * users = new http_connection[ MAX_FD ];

    int lis_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (lis_fd == -1)
    {
        perror("socket error");
        exit(-1);
    }
    printf("socket 设置成功\n");

    //端口复用,一定在绑定之前设置
    int reuse = 1;
    setsockopt(lis_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    printf("端口复用绑定成功\n");

    //绑定
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    int ret = bind(lis_fd, (sockaddr * )& addr, sizeof(addr));
    if (ret == -1)
    {
        perror("bind error");
        exit(-1);
    }

    //监听
    ret = listen(lis_fd, 5);
    if (ret == -1)
    {
        perror("listen error");
    }

    //创建epoll对象，事件数组
    epoll_event event[MAX_EVENTS];

    int epoll_fd = epoll_create(5);

    //把监听的文件描述符添加到epoll中
    add_fd(epoll_fd, lis_fd, false);
    http_connection::m_epoll_fd = epoll_fd;

    printf("开始循环监测...\n");

    //循环检测那些事件发生
    while(1)
    {
        int epoll_num = epoll_wait(epoll_fd, event, MAX_EVENTS, -1);
        if ((epoll_num < 0) && (errno != EINTR))
        {
            perror("epoll wait fail");
            exit(-1);
        }

        //循环遍历时间数组
        for (int i=0;i < epoll_num; i++)
        {
            int sock_fd = event[i].data.fd;
            if (sock_fd == lis_fd)
            {
                //客户端连接
                printf("有客户端连接\n");
                struct sockaddr_in client_addr;
                socklen_t len = sizeof(client_addr);
                int connect_fd = accept(lis_fd, (struct sockaddr * ) &client_addr, &len);
                if (connect_fd == -1)
                {
                    perror("accept error");
                    exit(-1);
                }

                //判断一下链接数量
                if (http_connection::m_user_count >= MAX_FD)
                {
                    //要给客户端发一个消息
                    close(connect_fd);
                    continue;
                }

                //把新数据初始化，放到用户数组
                users[connect_fd].init(connect_fd, client_addr);
            }
            else if (event[i].events & (EPOLLERR | EPOLLRDHUP | EPOLLHUP))
            {
                //如果对方异常或者错误等事件关闭连接
                users[sock_fd].close_conn();
                printf("对方异常，关闭连接\n");
            }
            else if (event[i].events & EPOLLIN)
            {
                printf("检测到读事件\n");
                //如果有读事件发生
                if (users[sock_fd].read()) //一次性读完所有数据
                {
                    pool->append(users + sock_fd); // 等同于 &user[sock_fd]

                    if (users[sock_fd].is_buffer_full())
                    {
                        modify_fd(epoll_fd, sock_fd, EPOLLIN);
                    }
                }
                else{
                    if(errno != EAGAIN && errno != EWOULDBLOCK) // 优化：传输大文件，可能read返回的是EAGAIN或者EWOULDBLOCK，此时不能关闭连接
                    {
                         printf("关闭此链接\n");
                        users[sock_fd].close_conn();
                    } 
                    else{
                        modify_fd(epoll_fd, sock_fd, EPOLLIN);
                    }
                }
            }
            else if (event[i].events & EPOLLOUT)
            {
                printf("处理写事件\n");
                if (!users[sock_fd].write()) // 一次性写完所有数据：如果一次性写不完呢？
                {
                    users[sock_fd].close_conn();
                }
            }
        }
    }

    close(epoll_fd);
    close(lis_fd);
    delete [] users;
    delete pool;

    return 0;
}