#pragma once
#include <stdbool.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include<string.h>

// 数据包类型
enum Type
{
    Heart,
    Message
};
// 数据包类型：'H'：心跳包，'M'：数据包
// 数据包格式：数据长度|数据包类型|数据块

// 初始化一个套接字
int initSocket();

// 初始化sockaddr结构体
void initSockaddr(struct sockaddr *addr, unsigned short port, const char *ip);

// 设置监听
int setListen(int lfd, unsigned short port);

// 接收客户端连接
int acceptConnect(int lfd, struct sockaddr *addr);

// 连接服务器
int connectToHost(int fd, unsigned short port, const char *ip);

// 读出指定字节数
int readn(int fd, char *buffer, int size);
// 写入指定字节数
int writen(int fd, const char *buffer, int length);

// 发送数据
bool sendMessage(int fd, const char *buffer, int length, enum Type t);
// 接收数据
int recvMessage(int fd, char **buffer, enum Type *t);