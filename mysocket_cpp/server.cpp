#include "my_socket.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <cstring>
#include <sys/eventfd.h>

int main()
{
    int lfd = initSocket();
    if (setListen(lfd, 8888) == -1)
    {
        close(lfd);
        return -1;
    }

    setNonBlock(lfd);

    // 创建epoll实例
    int epfd = epoll_create1(0);

    if (epfd == -1)
    {
        perror("epoll_create");
        return -1;
    }

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    wakeupFd = eventfd(0, EFD_NONBLOCK);
    if (wakeupFd == -1)
    {
        perror("eventfd");
        return -1;
    }
    ev.events = EPOLLIN;
    ev.data.fd = wakeupFd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, wakeupFd, &ev);

    epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);

    while (1)
    {
        int num = epoll_wait(epfd, evs, size, -1);
        if (num == -1)
        {
            if (errno == EINTR)
                continue;
            perror("epoll_wait");
            break;
        }

        printf("事件数 = %d\n", num);

        for (int i = 0; i < num; ++i)
        {
            int fd = evs[i].data.fd;
            uint32_t events = evs[i].events;
            if (fd == wakeupFd)
            {
                uint64_t val;
                read(wakeupFd, &val, sizeof(val));
                std::unique_lock<std::mutex> lock(resultMtx);
                while (!resultQueue.empty())
                {
                    Result r = std::move(resultQueue.front());
                    resultQueue.pop();
                    auto c = r.conn.lock();
                    if (!c)
                        continue; // 连接已销毁，丢弃
                    flushToConn(epfd, c->fd, r.data);
                }
            }
            else if (fd == lfd)
            {
                handleAccept(epfd, lfd);
            }
            else
            {
                if (events & (EPOLLHUP | EPOLLERR))
                {
                    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                    close(fd);
                    connMap.erase(fd);
                    continue;
                }
                if (events & EPOLLIN)
                    handleRead(epfd, fd);
                if (connMap.find(fd) == connMap.end())
                    continue; // 已经关了，跳过
                if (events & EPOLLOUT)
                    handleWrite(epfd, fd);
            }
        }
    }

    close(lfd);
    close(wakeupFd);
    close(epfd);

    return 0;
}