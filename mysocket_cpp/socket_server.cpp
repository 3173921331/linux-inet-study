#include "socket_server.h"
#include "ThreadPool.h"
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>

namespace
{
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
        std::string pkt = unPacket(info, t);

        {
            // 上锁并给结果队列添加数据
            std::unique_lock<std::mutex> lock(resultMtx);
            resultQueue.push({conn, std::move(pkt)});
        }
        uint64_t one = 1;
        // 唤醒结果队列
        write(wakeupFd, &one, sizeof(one));
    }
}

std::unordered_map<int, std::shared_ptr<Connection>> connMap;

std::queue<Result> resultQueue;
std::mutex resultMtx;

int wakeupFd = -1; // eventfd

ThreadPool pool(4); // 线程池

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

    conn->heartbeatCount = 0;

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
        if (t == Type::Heart)
        {
            std::string pkt = unPacket(info, t);
            flushToConn(epfd, fd, pkt);
            continue;
        }
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
