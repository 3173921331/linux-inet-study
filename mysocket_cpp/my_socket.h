#pragma once
#include <arpa/inet.h>
#include <string>
#include <fcntl.h>
#include <unordered_map>
#include <memory>
#include <chrono>

inline constexpr int MAX_MSG_SIZE = 10 * 1024;

// 数据包类型
enum class Type
{
    Heart,
    Message
};
// 数据包类型：'H'：心跳包，'M'：数据包
// 数据包格式：数据长度|数据包类型|数据块

struct Connection
{
    explicit Connection(int f) : fd(f) {}
    int fd;
    std::string recvBuf;    // 已收但未解析的字节
    std::string sendBuf;    // 待发送的字节
    bool needClose = false; // 标记待关闭
    // 后面加：lastActiveTime、heartbeatCount 等
};

extern std::unordered_map<int, std::shared_ptr<Connection>> connMap;

// 设置非阻塞
inline void setNonBlock(int &fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

// 初始化一个套接字
int initSocket();

// 初始化sockaddr结构体
bool initSockaddr(sockaddr &addr, unsigned short port, const std::string &ip);

// 设置监听
int setListen(int lfd, unsigned short port);

// 接收客户端连接
int acceptConnect(int lfd);
int acceptConnect(int lfd, sockaddr &addr);
// 循环接收客户端连接
void handleAccept(int epfd, int lfd);

// 连接到服务器
int connectToHost(int fd, unsigned short port, const std::string &ip);

// 读出指定字节数
int readn(int fd, std::string &buffer, int size);
// 写入指定字节数
int writen(int fd, const std::string &buffer, int length);
// 发送整个string, 不需要传入length
int writen(int fd, const std::string &buffer);

// 发送数据
bool sendMsg(int fd, const std::string &buffer, int length, Type t);
// 接收数据
// 返回：>0 消息体长度；0 对端关闭；-1 系统调用错误；-2 协议错误
int recvMsg(int fd, std::string &buffer, Type &t);

// 添加EPOLLOUT
void enableWrite(int fd, int epfd);

// 取消EPOLLOUT
void disableWrite(int fd, int epfd);

// 获取ConnMap实例
std::shared_ptr<Connection> getConn(int fd);

// 发送数据
void sendToConn(int epfd, int fd, const std::string &buffer, Type t);

// 循环读
void handleRead(int epfd, int fd);

// 循环写
void handleWrite(int epfd, int fd);