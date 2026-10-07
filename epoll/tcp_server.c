#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <errno.h>

int main()
{

    // 创建监听的套接字
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        exit(1);
    }

    // 2.绑定本地的IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);
    saddr.sin_addr.s_addr = INADDR_ANY; // 0 = 0.0.0.0, 自动绑定本地网卡的ip地址

    int ret = bind(lfd, (struct sockaddr *)&saddr, sizeof(saddr));

    if (ret == -1)
    {
        perror("bind");
        exit(1);
    }

    // 设置监听
    ret = listen(lfd, 128);

    if (ret == -1)
    {
        perror("listen error");
        exit(1);
    }

    // 创建epoll实例

    int epfd = epoll_create(1);

    if (epfd == -1)
    {
        perror("epoll_create");
        exit(0);
    }

    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.fd = lfd;
    epoll_ctl(epfd, EPOLL_CTL_ADD, lfd, &ev);

    struct epoll_event evs[1024];
    int size = sizeof(evs) / sizeof(evs[0]);

    while (1)
    {
        int num = epoll_wait(epfd, evs, size, -1);
        printf("num = %d\n", num);
        for (int i = 0; i < num; ++i)
        {
            int fd = evs[i].data.fd;
            if (fd == lfd)
            {
                int cfd = accept(fd, NULL, NULL);
                // 设置非阻塞属性
                int flag = fcntl(cfd, F_GETFL);
                flag |= O_NONBLOCK;
                fcntl(cfd, F_SETFL, flag);
                ev.events = EPOLLIN | EPOLLET;
                ev.data.fd = cfd;
                epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev);
            }
            else
            {
                // 接收数据
                char buf[5];
                while (1)
                {
                    int len = recv(fd, buf, sizeof(buf), 0);

                    if (len == -1)
                    {
                        if (errno == EAGAIN)
                        {
                            printf("数据已经接受完毕...\n");
                            break;
                        }
                        perror("recv error");
                        exit(1);
                    }
                    else if (len == 0)
                    {
                        printf("客户端已经断开了连接...\n");
                        epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL);
                        close(fd);
                        break;
                    }

                    printf("read buf = %s\n", buf);
                    // 小写转大写
                    for (int j = 0; j < len; ++j)
                    {
                        buf[j] = toupper(buf[j]);
                    }
                    write(STDOUT_FILENO, buf, len);
                    // printf("after buf = %s\n", buf);

                    // // 大写串发给客户端
                    ret = send(fd, buf, strlen(buf) + 1, 0);
                    if (ret == -1)
                    {
                        perror("send error");
                        exit(1);
                    }
                }
            }
        }
        sleep(1);
    }

    // 关闭文件描述符
    close(lfd);

    return 0;
}
