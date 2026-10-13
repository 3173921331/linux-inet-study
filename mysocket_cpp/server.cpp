#include "my_socket.h"
#include <unistd.h>

int main()
{
    int lfd = initSocket();
    if (setListen(lfd, 8888) == -1)
    {
        close(lfd);
        return -1;
    }

    sockaddr addr;
    int cfd = acceptConnect(lfd, addr);
    if (cfd == -1)
    {
        printf("acpt\n");
        return -1;
    }
    while (1)
    {
        std::string buff;
        Type t;
        int ret = recvMsg(cfd, buff, t);
        if (ret <= 0)
        {
            printf("recv = %d\n", ret);
            break;
        }
        buff += '\0';
        printf("收到：%s\n", buff.c_str());
        ret = sendMsg(cfd, buff, buff.size(), Type::Message);
        if (ret == false)
        {
            printf("send\n");
            break;
        }
        printf("发送: %s\n", buff.c_str());
    }

    close(cfd);
    close(lfd);

    return 0;
}