#include "socket.h"

int initSocket()
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd == -1)
    {
        perror("socket");
        return -1;
    }
    return lfd;
}

// 初始化sockaddr结构体
void initSockaddr(struct sockaddr *addr, unsigned short port, const char *ip)
{
    struct sockaddr_in *addrin = (struct sockaddr_in *)addr;
    addrin->sin_family = AF_INET;
    addrin->sin_port = htons(port);
    addrin->sin_addr.s_addr = inet_addr(ip);
}

int setListen(int lfd, unsigned short port)
{
    struct sockaddr addr;
    initSockaddr(&addr, port, "0.0.0.0"); // INARRD_ANY
    // 设置端口复用
    int opt = 1;
    setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
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

int acceptConnect(int lfd, struct sockaddr *addr)
{
    int connfd;
    if (addr == NULL)
    {
        connfd = accept(lfd, NULL, NULL);
    }
    else
    {
        socklen_t len = sizeof(struct sockaddr);
        connfd = accept(lfd, addr, &len);
    }
    if (connfd == -1)
    {
        perror("accept");
        return -1;
    }
    return connfd;
}

int connectToHost(int fd, unsigned short port, const char *ip)
{
    struct sockaddr addr;
    initSockaddr(&addr, port, ip);
    int ret = connect(fd, &addr, sizeof(addr));
    if (ret == -1)
    {
        perror("connect");
        return -1;
    }
    return 0;
}

int readn(int fd, char *buffer, int size)
{
    int left = size;
    int readBytes = 0;
    char *ptr = buffer;

    while (left)
    {
        readBytes = recv(fd, ptr, left, 0);
        if (readBytes == -1)
        {
            if (errno == EINTR)
            {
                readBytes = 0;
            }
            else
            {
                perror("read");
                return -1;
            }
        }
        else if (readBytes == 0)
        {
            printf("对方已经断开了连接...\n");
            return 0;
        }
        left -= readBytes;
        ptr += readBytes;
    }

    return size - left;
}

int writen(int fd, const char *buffer, int length)
{
    int left = length;
    int writeBytes = 0;
    const char *ptr = buffer;

    while (left)
    {
        writeBytes = send(fd, ptr, left, 0);
        if (writeBytes <= 0)
        {
            if (errno == EINTR)
            {
                writeBytes = 0;
            }
            else
            {
                perror("send");
                return -1;
            }
        }
        ptr += writeBytes;
        left -= writeBytes;
    }
    return length - left;
}

// 发送数据
bool sendMessage(int fd, const char *buffer, int length, enum Type t)
{
    int dataLen = length + 1 + sizeof(int);
    char *data = (char *)malloc(dataLen);
    if (data == NULL)
    {
        return false;
    }
    int netlen = htonl(length + 1);
    memcpy(data, &netlen, sizeof(int));
    char *ch = t == Heart ? "H" : "M";
    memcpy(data + sizeof(int), ch, sizeof(char));
    memcpy(data + sizeof(int) + 1, buffer, length);
    int ret = send(fd, data, dataLen, 0);
    free(data);
    return ret == dataLen;
}
// 接收数据
int recvMessage(int fd, char **buffer, enum Type *t)
{
    int dataLen = 0;
    int ret = readn(fd, (char*)&dataLen, sizeof(int));
    if (ret == -1)
    {
        *buffer = NULL;
        return -1;
    }
    else if(ret == 0)
    {
        *buffer = NULL;
        return 0;
    }
    dataLen = ntohl(dataLen);
    char ch;
    ret = readn(fd, &ch, 1);
    *t = ch == 'H' ? Heart : Message;
    char *tmpbuf = (char *)calloc(dataLen, sizeof(char));
    if(tmpbuf == NULL)
    {
        *buffer = NULL;
        return -1;
    }
    ret = readn(fd, tmpbuf, dataLen - 1);
    if(ret != dataLen - 1)
    {
        free(tmpbuf);
        *buffer = NULL;
        return -1;
    }
    *buffer = tmpbuf;
    return ret;
}