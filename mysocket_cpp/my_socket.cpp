#include "my_socket.h"
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include<climits>

int initSocket()
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("initSocket");
        return -1;
    }
    return fd;
}

bool initSockaddr(sockaddr &addr, unsigned short port, const std::string &ip)
{
    memset(&addr, 0, sizeof(addr));
    sockaddr_in *addrin = (sockaddr_in *)&addr;
    addrin->sin_family = AF_INET;
    addrin->sin_port = htons(port);
    int ret = inet_pton(AF_INET, ip.c_str(), &addrin->sin_addr);
    if (ret <= 0)
    {
        printf("inet_pton failed for ip: %s\n", ip.c_str());
        return false;
    }
    return true;
}

int setListen(int lfd, unsigned short port)
{
    sockaddr addr;
    if (!initSockaddr(addr, port, "0.0.0.0"))
    {
        return -1;
    }
    int ret = bind(lfd, &addr, sizeof(addr));
    if (ret == -1)
    {
        perror("bind");
        return -1;
    }
    ret = listen(lfd, 128);
    if (ret == -1)
    {
        perror("listen");
        return -1;
    }
    return 0;
}

int acceptConnect(int lfd)
{
    int cfd = accept(lfd, nullptr, nullptr);
    if (cfd == -1)
    {
        perror("accept");
        return -1;
    }
    return cfd;
}

int acceptConnect(int lfd, sockaddr &addr)
{
    int cfd;
    socklen_t len = sizeof(sockaddr);
    cfd = accept(lfd, &addr, &len);
    if (cfd == -1)
    {
        perror("accept");
        return -1;
    }
    return cfd;
}

int connectToHost(int fd, unsigned short port, const std::string &ip)
{
    sockaddr addr;
    if (!initSockaddr(addr, port, ip))
    {
        return -1;
    }
    socklen_t len = sizeof(addr);
    int ret = connect(fd, &addr, len);
    if (ret == -1)
    {
        perror("connect");
        return -1;
    }
    return 0;
}

int readn(int fd, std::string &buffer, int size)
{
    if (size < 0)
        return -1;
    buffer.clear();
    buffer.resize(size);
    int left = size;
    int readBytes = 0;

    while (left > 0)
    {
        int ret = recv(fd, &buffer[0] + readBytes, left, 0);
        if (ret == -1)
        {
            if (errno == EINTR)
                // 被信号打断，不算失败，继续循环读取
                continue;
            else
            {

                perror("readn");
                return -1;
            }
        }
        else if (ret == 0)
        {
            printf("对方已经断开了连接...\n");
            buffer.clear();
            return 0;
        }
        left -= ret;
        readBytes += ret;
    }

    return readBytes;
}

int writen(int fd, const std::string &buffer, int length)
{
    if (length < 0)
        return -1;
    int left = length;
    int writeBytes = 0;

    while (left > 0)
    {
        int ret = send(fd, &buffer[0] + writeBytes, left, 0);
        if (ret <= 0)
        {
            if (errno == EINTR)
                continue;
            else
            {
                perror("writen");
                return -1;
            }
        }
        left -= ret;
        writeBytes += ret;
    }
    return writeBytes;
}

// 版本：直接发送整个string，不需要外面传length
int writen(int fd, const std::string &buffer)
{
    if (buffer.size() > INT_MAX)
        return -1;
    return writen(fd, buffer, buffer.size());
}

bool sendMsg(int fd, const std::string &buffer, int length, Type t)
{
    if (length < 0 || length > (int)buffer.size())
        return false;
    std::string data;
    int pkgLen = length + 1 + sizeof(int);
    data.reserve(pkgLen);
    int netlen = htonl(length + 1);
    data.append(reinterpret_cast<const char *>(&netlen), sizeof(netlen));
    data += t == Type::Heart ? 'H' : 'M';
    data.append(buffer.data(), length);
    int ret = writen(fd, data);
    return ret == pkgLen;
}

int recvMsg(int fd, std::string &buffer, Type &t)
{
    uint32_t netLen;
    std::string tmp;
    int ret = readn(fd, tmp, sizeof(netLen));
    if (ret != sizeof(netLen))
        return ret;
    memcpy(&netLen, tmp.data(), sizeof(netLen));
    netLen = ntohl(netLen);

    if (netLen < 1 || netLen > MAX_MSG_SIZE)
        return -2;

    ret = readn(fd, tmp, 1);
    if (ret != 1)
        return ret;
    if (tmp[0] == 'H')
        t = Type::Heart;
    else if (tmp[0] == 'M')
        t = Type::Message;
    else
        return -2;

    if (netLen == 1)
    {
        buffer.clear();
        return 1;
    }

    ret = readn(fd, buffer, netLen - 1);
    return ret;
}
