#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <ctype.h>

int main()
{

    // 创建监听的套接字
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1)
    {
        perror("socket");
        return -1;
    }

    // 2.绑定本地的IP port
    struct sockaddr_in saddr;
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(9999);
    saddr.sin_addr.s_addr = INADDR_ANY; // 0 = 0.0.0.0, 自动绑定本地网卡的ip地址

    int ret = bind(fd, (struct sockaddr *)&saddr, sizeof(saddr));

    if (ret == -1)
    {
        perror("bind");
        return -1;
    }

    // 设置监听
    ret = listen(fd, 128);

    fd_set readset;
    FD_ZERO(&readset);
    FD_SET(fd, &readset);

    int maxfd = fd;
    while (1)
    {
        fd_set tmp = readset;
        int ret = select(maxfd + 1, &tmp, NULL, NULL, NULL);
        // 判断是不是监听的fd
        if (FD_ISSET(fd, &tmp))
        {
            // 接受客户端的连接
            int cfd = accept(fd, NULL, NULL);
            FD_SET(cfd, &readset);
            maxfd = cfd > maxfd ? cfd : maxfd;
        }
        for (int i = 0; i <= maxfd; ++i)
        {
            if (i != fd && FD_ISSET(i, &tmp))
            {
                // 接收数据
                char buf[1024];
                int len = recv(i, buf, sizeof(buf) + 1, 0);

                if (len == -1)
                {
                    perror("recv error");
                    exit(1);
                }
                else if (len == 0)
                {
                    printf("客户端已经断开了连接...\n");
                    FD_CLR(i, &readset);
                    close(i);
                    continue;
                }

                printf("read buf = %s\n", buf);
                // 小写转大写
                for (int i = 0; i < len; ++i)
                {
                    buf[i] = toupper(buf[i]);
                }
                printf("after buf = %s\n", buf);

                // 大写串发给客户端
                ret = send(i, buf, strlen(buf) + 1, 0);
                if (ret == -1)
                {
                    perror("send error");
                    exit(1);
                }
            }
        }
    }

    // 关闭文件描述符
    close(fd);

    return 0;
}
