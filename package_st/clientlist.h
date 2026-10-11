#pragma once
#include <stdbool.h>
#include <pthread.h>

// 定义链表的节点
struct ClientInfo
{
    int fd; // 通信
    int count;
    pthread_t tid;
    int dead;
    struct ClientInfo *head;
    struct ClientInfo *next;
};

// 创建一个链表，带头节点，返回头节点
struct ClientInfo *createList();

// 添加一个节点（头插法）， 返回这个节点的地址
struct ClientInfo *prependNode(struct ClientInfo *head, int fd);

bool removeNode(struct ClientInfo *head, int fd);

void freeCliList(struct ClientInfo *head);