#include "socket_common.h"
#include <iostream>
#include <unistd.h>
#include <csignal>

pthread_mutex_t mtx;

typedef struct FdInfo
{
    int fd;
    int count; // 记录有多少次没有收到服务器回复的数据
} fdInfo;

// 1、发送心跳包数据
// 2、检测心跳包，看看能否收到服务器回复的数据
void *heartBeat(void *arg)
{
    fdInfo *info = (fdInfo *)arg;
    while (1)
    {
        pthread_mutex_lock(&mtx);
        info->count++; // 默认没收到服务器回复的心跳包数据
        printf("fd = %d, count = %d\n", info->fd, info->count);
        if (info->count > 5)
        {
            // 客户端和服务端断开了连接
            printf("客户端和服务器断开了连接...\n");
            close(info->fd);
            // 释放套接字资源，退出客户端程序
            exit(0);
        }
        pthread_mutex_unlock(&mtx);
        sleep(3);
    }
    return NULL;
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    fdInfo info;
    info.count = 0;
    info.fd = initSocket();
    sockaddr addr;
    unsigned short port = 8888;
    const std::string ip = "192.168.116.130";

    if (connectToHost(info.fd, port, ip) == -1)
    {
        close(info.fd);
        return -1;
    }

    pthread_mutex_init(&mtx, NULL);

    // 添加心跳包子线程
    pthread_t tid;
    pthread_create(&tid, NULL, heartBeat, &info);

    int num = 0;
    while (1)
    {
        const std::string buff = "你好, 世界...";
        printf("要发送的：%s\n", buff.c_str());
        // 发送
        if (!sendMsg(info.fd, buff, buff.size(), Type::Message))
        {
            printf("发送失败！\n");
            break;
        }

        if (!sendMsg(info.fd, "hello", 5, Type::Heart))
        {
            printf("发送失败！\n");
            break;
        }

        std::string get;
        Type t;
        int ret = recvMsg(info.fd, get, t);
        if (ret <= 0)
        {
            break;
        }

        pthread_mutex_lock(&mtx);
        info.count = 0;
        pthread_mutex_unlock(&mtx);
        if (t == Type::Heart)
        {
            printf("收到心跳包：%s\n", get.c_str());
        }
        else
        {
            printf("收到：%s, num = %d\n", get.c_str(), num++);
            sleep(2);
        }
    }

    close(info.fd);
    return 0;
}