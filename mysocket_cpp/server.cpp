#include "socket_server.h"
#include <unistd.h>
#include <cstring>
#include <csignal>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>

int main()
{
    signal(SIGPIPE, SIG_IGN);
    // 初始化套接字
    int lfd = initSocket();
    // 设置监听
    if (setListen(lfd, 8888) == -1)
    {
        close(lfd);
        return -1;
    }

    // 设置非阻塞
    setNonBlock(lfd);

    // 创建epoll实例
    int epfd = epoll_create1(0);

    if (epfd == -1)
    {
        perror("epoll_create");
        return -1;
    }

    // 添加服务端套接字到epoll实例中
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    // 创建结果到达的唤醒的事件描述符，并添加到epoll中
    wakeupFd = eventfd(0, EFD_NONBLOCK);
    if (wakeupFd == -1)
    {
        perror("eventfd");
        return -1;
    }
    ev.events = EPOLLIN;
    ev.data.fd = wakeupFd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, wakeupFd, &ev);

    //
    int timerfd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK);
    if (timerfd == -1)
    {
        perror("timerfd_create");
        return -1;
    }
    itimerspec its{};
    its.it_interval.tv_sec = 3; // 每 3 秒
    its.it_value.tv_sec = 3;
    timerfd_settime(timerfd, 0, &its, nullptr);
    ev.events = EPOLLIN;
    ev.data.fd = timerfd;
    // epoll_ctl(epfd, EPOLL_CTL_ADD, timerfd, &ev);
    if (epoll_ctl(epfd, EPOLL_CTL_ADD, timerfd, &ev) == -1)
    {
        perror("epoll_ctl ADD timerfd");
        return -1;
    }

    // 创建用于接收epoll输出的events数组
    epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);

    while (1)
    {
        // 阻塞等待就绪事件
        int num = epoll_wait(epfd, evs, size, -1);
        if (num == -1)
        {
            // 被信号打断，重新等待
            if (errno == EINTR)
                continue;
            // 调用出错
            perror("epoll_wait");
            break;
        }

        // 日志信息
        printf("事件数 = %d\n", num);

        // 处理events数组中的内容
        for (int i = 0; i < num; ++i)
        {
            int fd = evs[i].data.fd;
            uint32_t events = evs[i].events;
            if (fd == timerfd)
            {
                uint64_t exp;
                read(timerfd, &exp, sizeof(exp));

                for (auto it = connMap.begin(); it != connMap.end();)
                {
                    auto &[cfd, conn] = *it;

                    conn->heartbeatCount++;
                    printf("fd = %d, count = %d\n", conn->fd, conn->heartbeatCount);
                    if (conn->heartbeatCount > 5)
                    {
                        epoll_ctl(epfd, EPOLL_CTL_DEL, cfd, nullptr);
                        close(cfd);
                        it = connMap.erase(it); // 返回下一个迭代器
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            else if (fd == wakeupFd)
            {
                uint64_t val;
                // 清空wakeupFd
                read(wakeupFd, &val, sizeof(val));
                // 上锁，防止处理数据时被别的线程添加数据
                std::vector<Result> batch;
                {
                    std::unique_lock<std::mutex> lock(resultMtx);
                    while (!resultQueue.empty())
                    {
                        batch.push_back(std::move(resultQueue.front()));
                        resultQueue.pop();
                    }
                }
                for (auto &r : batch)
                {
                    // r中的conn是weak_ptr，尝试提升成shared_ptr，对象还在就成功
                    auto c = r.conn.lock();
                    if (!c)
                        continue; // 连接已销毁，丢弃
                    // 处理数据
                    flushToConn(epfd, c->fd, r.data);
                }
            }
            else if (fd == lfd)
            {
                // 循环接受连接
                handleAccept(epfd, lfd);
            }
            else
            {
                // 判断是否是挂起或者发生错误
                if (events & (EPOLLHUP | EPOLLERR))
                {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    connMap.erase(fd);
                    continue;
                }
                // 是不是对方发来数据
                if (events & EPOLLIN)
                    handleRead(epfd, fd);
                if (connMap.find(fd) == connMap.end())
                    continue; // 已经关了，跳过
                // 是不是有需要发送的数据
                if (events & EPOLLOUT)
                    handleWrite(epfd, fd);
            }
        }
    }

    // 关闭套接字
    close(lfd);
    close(wakeupFd);
    close(epfd);
    close(timerfd);

    return 0;
}