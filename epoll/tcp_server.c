#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>

typedef struct socketinfo
{
    int fd;
    int epfd;
} SocketInfo;

void *acceptConn(void *arg)
{
    printf("acceptconn tid: %ld\n", pthread_self());
    struct epoll_event ev;
    SocketInfo *info = (SocketInfo *)arg;
    int cfd = accept(info->fd, NULL, NULL);
    // 设置非阻塞属性
    int flag = fcntl(cfd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(cfd, F_SETFL, flag);
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = cfd;
    epoll_ctl(info->epfd, EPOLL_CTL_ADD, cfd, &ev);

    free(info);
    return NULL;
}

void *communication(void *arg)
{
    printf("communication tid: %ld\n", pthread_self());
    SocketInfo *info = (SocketInfo *)arg;
    int fd = info->fd;
    int epfd = info->epfd;
    char buf[6];
    char temp[1024];
    bzero(temp, sizeof(temp));
    while (1)
    {
        int len = recv(fd, buf, sizeof(buf) - 1, 0);

        if (len == -1)
        {
            if (errno == EAGAIN)
            {
                printf("数据已经接受完毕...\n");
                printf("after buf = %s\n", temp);
                send(fd, temp, strlen(temp) + 1, 0);
                break;
            }
            else
            {
                perror("recv error");
                break;
            }
        }
        else if (len == 0)
        {
            printf("客户端已经断开了连接...\n");
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            break;
        }
        buf[len] = '\0';
        printf("read buf = %s\n", buf);
        // 小写转大写
        for (int j = 0; j < len; ++j)
        {
            buf[j] = toupper(buf[j]);
        }
        strncat(temp + strlen(temp), buf, len);
    }

    free(info);
    return NULL;
}

int main()
{

    // 创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        exit(1);
    }

    // 2.绑定本地的IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);
    saddr.sin_addr.s_addr = INADDR_ANY; // 0 = 0.0.0.0, 自动绑定本地网卡的ip地址

    int ret = bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));

    if (ret == -1)
    {
        perror("bind");
        exit(1);
    }

    // 设置监听
    ret = listen(lfd, 128);

    if (ret == -1)
    {
        perror("listen error");
        exit(1);
    }

    // 创建epoll实例

    int epfd = epoll_create(1);

    if (epfd == -1)
    {
        perror("epoll_create");
        exit(0);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);

    while (1)
    {
        int num = epoll_wait(epfd, evs, size, -1);
        printf("num = %d\n", num);
        pthread_t tid;
        for (int i = 0; i < num; ++i)
        {
            int fd = evs[i].data.fd;
            SocketInfo *info = (SocketInfo *)malloc(sizeof(SocketInfo));
            info->fd = fd;
            info->epfd = epfd;
            if (fd == lfd)
            {
                pthread_create(&tid, NULL, acceptConn, info);
                pthread_detach(tid);
            }
            else
            {
                pthread_create(&tid, NULL, communication, info);
                pthread_detach(tid);
            }
        }
        sleep(1);
    }

    // 关闭文件描述符
    close(lfd);

    return 0;
}

// epoll内部函数是线程安全，所以不需要考虑线程安全问题，直接使用即可，如果有其他信息修改还是需要加锁
