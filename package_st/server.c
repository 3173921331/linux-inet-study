#include "socket.h"
#include "clientlist.h"
#include <pthread.h>
#include<unistd.h>

pthread_mutex_t mtx;

void *parseRecvMessage(void *arg)
{
    struct ClientInfo *info = (struct ClientInfo *)arg;
    while (1)
    {
        char *buffer;
        enum Type t;
        int len = recvMessage(info->fd, &buffer, &t);
        if (buffer == NULL)
        {
            printf("fd = %d, 通信的子线程退出了...\n", info->fd);
            pthread_exit(NULL);
        }
        else
        {
            if (t == Heart)
            {
                printf("心跳包...%s\n", buffer);
                pthread_mutex_lock(&mtx);
                info->count = 0;
                pthread_mutex_unlock(&mtx);
                sendMessage(info->fd, buffer, len, Heart);
            }
            else
            {
                const char *pt = "愿世界和平...";
                printf("数据包: %s\n", buffer);
                sendMessage(info->fd, pt, strlen(pt), Heart);
            }
            free(buffer);
        }
    }
    return NULL;
}

// 1、发送心跳包数据
// 2、检测心跳包，看看能否收到服务器回复的数据
void *heartBeat(void *arg)
{
    struct ClientInfo *head = (struct ClientInfo *)arg;
    struct ClientInfo *p = NULL;
    struct ClientInfo *nxp = NULL;
    while (1)
    {
        pthread_mutex_lock(&mtx);
        p = head->next;
        pthread_mutex_unlock(&mtx);
        while (p)
        {
            pthread_mutex_lock(&mtx);
            nxp = p->next;
            p->count++; // 默认没收到服务器回复的心跳包数据
            printf("fd = %d, count = %d\n", p->fd, p->count);
            if (p->count > 5)
            {
                // 客户端和服务端断开了连接
                printf("客户端 fd = %d, 客户端和服务器断开了连接...\n", p->fd);
                close(p->fd);
                // 释放套接字资源，退出客户端程序
                pthread_cancel(p->tid);
                removeNode(head, p->fd);
            }
            pthread_mutex_unlock(&mtx);
            p = nxp;
        }
        sleep(3);
    }
    return NULL;
}

int main()
{
    unsigned short port = 8888;
    const char *ip = "127.0.0.1";
    int lfd = initSocket();

    setListen(lfd, port);

    // 创建链表
    struct ClientInfo *head = createList();

    pthread_mutex_init(&mtx, NULL);

    // 添加心跳包子线程
    pthread_t tid2;
    pthread_create(&tid2, NULL, heartBeat, head);

    while (1)
    {
        int sockfd = acceptConnect(lfd, NULL);
        if (sockfd == -1)
        {
            continue;
        }
        pthread_mutex_lock(&mtx);
        struct ClientInfo *node = prependNode(head, sockfd);
        pthread_mutex_unlock(&mtx);
        // 创建接收数据的子线程
        pthread_create(&node->tid, NULL, parseRecvMessage, node);

        pthread_detach(node->tid);
    }

    pthread_join(tid2, NULL);
    pthread_mutex_destroy(&mtx);
    close(lfd);
    return 0;
}