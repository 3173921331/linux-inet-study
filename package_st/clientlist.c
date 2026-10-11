#include "clientlist.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

struct ClientInfo *createList()
{
    struct ClientInfo *head = (struct ClientInfo *)malloc(sizeof(struct ClientInfo));
    head->next = NULL;
    return head;
}

struct ClientInfo *prependNode(struct ClientInfo *head, int fd)
{
    struct ClientInfo *node = (struct ClientInfo *)malloc(sizeof(struct ClientInfo));
    node->fd = fd;
    node->count = 0;
    node->dead = 0;
    node->next = head->next;
    head->next = node;
    return node;
}

bool removeNode(struct ClientInfo *head, int fd)
{
    struct ClientInfo *p = head;
    struct ClientInfo *q = head->next;
    while (q)
    {
        if (q->fd == fd)
        {
            p->next = q->next;
            free(q);
            printf("成功将链表中的 fd 节点删除了, fd = %d\n", fd);
            return true;
        }
        p = p->next;
        q = q->next;
    }
    return false;
}

void freeCliList(struct ClientInfo *head)
{
    struct ClientInfo *p = head;
    while (p)
    {
        struct ClientInfo *tmp = p;
        p = p->next;
        free(tmp);
    }
}
