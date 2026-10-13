#include <iostream>
#include <unistd.h>
#include "my_socket.h"

int main()
{
    int fd = initSocket();
    sockaddr addr;
    unsigned short port = 8888;
    const std::string ip = "127.0.0.1";

    if (connectToHost(fd, port, ip) == -1)
    {
        close(fd);
        return -1;
    }

    int num = 0;
    while (1)
    {
        const std::string buff = "你好, 世界...";
        printf("要发送的：%s\n", buff.c_str());
        // 发送
        if (!sendMsg(fd, buff, buff.size(), Type::Message))
        {
            printf("发送失败！\n");
            break;
        }

        std::string get;
        Type t;
        int ret = recvMsg(fd, get, t);
        if (ret <= 0)
        {
            break;
        }
        printf("收到：%s, num = %d\n", get.c_str(), num++);
        sleep(2);
    }

    close(fd);
    return 0;
}