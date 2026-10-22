#pragma once
#include "socket_common.h"
#include <memory>
#include <unordered_map>
#include <queue>
#include <mutex>

struct Connection
{
    explicit Connection(int f) : fd(f) {}
    int fd;
    std::string recvBuf; // 已收但未解析的字节
    std::string sendBuf; // 待发送的字节
    // 后面加：lastActiveTime、heartbeatCount 等
    int heartbeatCount = 0;
};

struct Result
{
    std::weak_ptr<Connection> conn;
    std::string data;
};

extern std::unordered_map<int, std::shared_ptr<Connection>> connMap;

extern int wakeupFd; // eventfd
extern std::queue<Result> resultQueue;
extern std::mutex resultMtx;

// 循环接收客户端连接
void handleAccept(int epfd, int lfd);


// 发送已经打包好的数据
void flushToConn(int epfd, int fd, const std::string &buffer);

// 循环读
void handleRead(int epfd, int fd);

void handleWrite(int epfd, int fd);