#include "my_socket.h"
#include "ThreadPool.h"
#include <cstring>
#include <unistd.h>
#include <cstdio>
#include <climits>
#include <sys/epoll.h>

std::unordered_map<int, std::shared_ptr<Connection>> connMap;

std::queue<Result> resultQueue;
std::mutex resultMtx;

int wakeupFd = -1; // eventfd

ThreadPool pool(4); // 线程池

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

void handleAccept(int epfd, int lfd)
{
    while (1)
    {
        sockaddr addr;
        int cfd;
        socklen_t len = sizeof(sockaddr);
        cfd = accept(lfd, &addr, &len);
        if (cfd == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // 没有连接了
                break;
            }
            if (errno == EINTR)
            {
                // 被中断
                continue;
            }
            else if (errno == EMFILE || errno == ENFILE)
            {
                // fd 耗尽
                printf("fd limit\n");
                break;
            }
            break;
        }
        setNonBlock(cfd);

        connMap.emplace(cfd, std::make_shared<Connection>(cfd));

        std::string ip(INET_ADDRSTRLEN, '\0');
        printf("已连接到客户端 ip: %s, 端口: %d, 目前已连接客户端 %d 个\n", inet_ntop(AF_INET, &((sockaddr_in *)&addr)->sin_addr, ip.data(), ip.size()), ntohs(((sockaddr_in *)&addr)->sin_port), (int)connMap.size());

        epoll_event ev;
        ev.events = EPOLLIN;
        ev.data.fd = cfd;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) == -1)
        {
            perror("epoll_ctl ADD");
            close(cfd);
            connMap.erase(cfd);
        }
    }
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
            else if (errno == EAGAIN)
            {
                // 没有更多数据，返回已读部分，交给调用者决定
                buffer.resize(readBytes);
                return readBytes;
            }
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
            else if (errno == EAGAIN)
            {
                continue;
            }
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

void enableWrite(int fd, int epfd)
{
    epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        perror("epoll_ctl MOD");
    }
}

void disableWrite(int fd, int epfd)
{
    epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) == -1)
    {
        perror("epoll_ctl MOD");
    }
}

std::shared_ptr<Connection> getConn(int fd)
{
    auto it = connMap.find(fd);
    if (it == connMap.end())
        return nullptr;
    return it->second;
}

void worker(std::weak_ptr<Connection> conn, std::string info, Type t)
{
    // 组包
    std::string pkt;
    int len = info.size();
    uint32_t pkgLen = len + 1 + sizeof(int);
    pkt.reserve(pkgLen);
    uint32_t netlen = htonl(len + 1);
    pkt.append((char *)(&netlen), sizeof(netlen));
    pkt += t == Type::Heart ? 'H' : 'M';
    pkt.append(info.data(), len);

    {
        // 上锁并给结果队列添加数据
        std::unique_lock<std::mutex> lock(resultMtx);
        resultQueue.push({conn, std::move(pkt)});
    }
    uint64_t one = 1;
    // 唤醒结果队列
    write(wakeupFd, &one, sizeof(one));
}

void flushToConn(int epfd, int fd, const std::string &buffer)
{
    auto conn = getConn(fd);
    if (!conn)
        return;

    // 如果 sendBuf 非空，说明还有数据没发完，直接追加
    if (!conn->sendBuf.empty())
    {
        conn->sendBuf.append(buffer);
        enableWrite(epfd, fd);
        return;
    }

    // 尝试直接发送
    printf("发送数据\n");
    int num = send(fd, buffer.data(), buffer.size(), 0);
    if (num == -1)
    {
        // 非阻塞socket：缓冲区满(EAGAIN) / 被信号打断(EINTR)，都暂存数据，注册写事件等后续发送
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
        {
            conn->sendBuf.append(buffer);
            enableWrite(epfd, fd);
            return;
        }
        perror("send");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        connMap.erase(fd);
        return;
    }
    // 没发完
    if ((size_t)num < buffer.size())
    {
        conn->sendBuf.append(buffer.data() + num, buffer.size() - num);
        enableWrite(epfd, fd);
    }
    // n == pkt.size() 表示一次发完，不用注册 EPOLLOUT
}

void handleRead(int epfd, int fd)
{
    auto conn = getConn(fd);
    if (!conn)
        return;
    char tmp[4096];
    // 循环读取缓冲区
    while (1)
    {
        int num = recv(fd, tmp, sizeof(tmp), 0);
        if (num > 0)
        {
            conn->recvBuf.append(tmp, num);
            continue;
        }
        if (num == 0)
        {
            printf("对方关闭了连接...\n");
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            connMap.erase(fd);
            return;
        }
        if (errno == EINTR)
            continue;
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            break;
        perror("recv");
        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
        close(fd);
        connMap.erase(fd);
        return;
    }

    // 读完从recvBuf里解析完整包
    while (1)
    {
        if (conn->recvBuf.size() < 4)
            break;
        uint32_t netLen;
        memcpy(&netLen, conn->recvBuf.data(), 4);
        netLen = ntohl(netLen);

        if (netLen < 1 || netLen > MAX_MSG_SIZE)
        {
            printf("fd = %d 协议错误 netLen = %u\n", fd, netLen);
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            connMap.erase(fd);
            return;
        }

        uint32_t totalLen = 4 + netLen;
        // 没接收完整包,break下次重新处理
        if (conn->recvBuf.size() < totalLen)
            break;

        // 接收了完整包
        Type t = conn->recvBuf[4] == 'H' ? Type::Heart : Type::Message;
        std::string info = conn->recvBuf.substr(5, netLen - 1);
        conn->recvBuf.erase(0, totalLen);

        // 业务处理
        printf("收到数据： %.*s\n", (int)info.size(), info.data());
        std::weak_ptr<Connection> wconn = conn;
        pool.enqueue([wconn, info, t]()
                     {
                        // 避免异常向上抛给packaged_task，防止临时future析构触发terminate干掉整个服务
                        try
                        {
                            worker(wconn, info, t);
                        }
                        catch (const std::exception& e)
                        {
                            // 标准异常，比如runtime_error等
                            printf("worker异常: %s\n", e.what());   
                        }
                        catch (...)
                        {
                            // 捕获所有其他异常（原生异常、自定义非std异常）
                            printf("worker发生未知异常\n");
                        } });
    }
}

void handleWrite(int epfd, int fd)
{
    auto conn = getConn(fd);
    if (!conn)
        return;
    while (!conn->sendBuf.empty())
    {
        printf("发送数据\n");
        int n = send(fd, conn->sendBuf.data(), conn->sendBuf.size(), 0);
        if (n > 0)
        {
            conn->sendBuf.erase(0, n);
            continue;
        }
        if (n == -1)
        {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return; // 等下次
            perror("send");
            epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
            close(fd);
            connMap.erase(fd);
            return;
        }
        if (n == 0)
            return;
    }
    // 发完了，取消 EPOLLOUT
    disableWrite(epfd, fd);
}