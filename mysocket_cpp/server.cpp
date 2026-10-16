#include "my_socket.h"
#include <unistd.h>
#include <sys/epoll.h>
#include <memory>
#include <cstring>


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
            if (fd == lfd)
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

    return 0;
}