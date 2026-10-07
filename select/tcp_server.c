#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <pthread.h>

pthread_mutex_t mutex;

typedef struct fdInfo
{
    int fd;
    int *maxfd;
    fd_set *rdset;
} fdInfo;

void *acceptConn(void *arg)
{
    printf("子线程线程ID: %ld\n", pthread_self());
    fdInfo *info = (fdInfo *)arg;

    int cfd = accept(info->fd, NULL, NULL);
    pthread_mutex_lock(&mutex);
    FD_SET(cfd, info->rdset);
    *info->maxfd = cfd > *info->maxfd ? cfd : *info->maxfd;
    pthread_mutex_unlock(&mutex);

    free(info);

    return NULL;
}

void *communication(void *arg)
{
    printf("连接中线程ID: %ld\n", pthread_self());
    fdInfo *info = (fdInfo *)arg;

    char buf[1024];
    int len = recv(info->fd, buf, sizeof(buf) + 1, 0);

    if (len == -1)
    {
        perror("recv error");
        close(info->fd);
        free(info);
        return NULL;
    }
    else if (len == 0)
    {
        printf("客户端已经断开了连接...\n");
        pthread_mutex_lock(&mutex);
        FD_CLR(info->fd, info->rdset);
        pthread_mutex_unlock(&mutex);
        close(info->fd);
        free(info);
        return NULL;
    }

    printf("read buf = %s\n", buf);
    // 小写转大写
    for (int i = 0; i < len; ++i)
    {
        buf[i] = toupper(buf[i]);
    }
    printf("after buf = %s\n", buf);

    // 大写串发给客户端
    int ret = send(info->fd, buf, strlen(buf), 0);
    if (ret == -1)
    {
        perror("send error");
    }
    free(info);
    return NULL;
}

int main()
{
    pthread_mutex_init(&mutex, NULL);
    // 创建监听的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket");
        return -1;
    }

    // 2.绑定本地的IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);
    saddr.sin_addr.s_addr = INADDR_ANY; // 0 = 0.0.0.0, 自动绑定本地网卡的ip地址

    int ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));

    if (ret == -1)
    {
        perror("bind");
        return -1;
    }

    // 设置监听
    ret = listen(fd, 128);
    if (ret == -1)
    {
        printf("listen error");
        exit(1);
    }

    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(fd, &readset);

    int maxfd = fd;
    while (1)
    {
        pthread_mutex_lock(&mutex);
        fd_set tmp = readset;
        pthread_mutex_unlock(&mutex);
        int ret = select(maxfd + 1, &tmp, NULL, NULL, NULL);
        // 判断是不是监听的fd
        if (FD_ISSET(fd, &tmp))
        {
            // 接受客户端的连接
            // 创建子线程
            pthread_t tid;
            fdInfo *info = (fdInfo *)malloc(sizeof(fdInfo));
            info->fd = fd;
            info->maxfd = &maxfd;
            info->rdset = &readset;
            pthread_create(&tid, NULL, acceptConn, info);
            pthread_detach(tid);
        }
        for (int i = 0; i <= maxfd; ++i)
        {
            if (i != fd && FD_ISSET(i, &tmp))
            {
                // 接收数据
                pthread_t tid;
                fdInfo *info = (fdInfo *)malloc(sizeof(fdInfo));
                info->fd = i;
                info->rdset = &readset;
                pthread_create(&tid, NULL, communication, info);
                pthread_detach(tid);
            }
        }
    }

    // 关闭文件描述符
    close(fd);
    pthread_mutex_destroy(&mutex);

    return 0;
}
