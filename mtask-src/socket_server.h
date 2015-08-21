//
//  socket_server.h
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__socket_server__
#define __mtask__socket_server__

#include <stdint.h>

#define SOCKET_DATA                 0
#define SOCKET_CLOSE                1
#define SOCKET_OPEN                 2
#define SOCKET_ACCEPT               3
#define SOCKET_ERROR                4
#define SOCKET_EXIT                 5
#define SOCKET_UDP                  6

struct socket_server;




struct socket_message {
    int id;             /*socket id*/
    uintptr_t opaque;   /*module id*/
    int ud;             /*for accept,ud is listen id; for data,ud is size of data*/
    char *data;         /*payload*/
};


struct socket_server * socket_server_create();


void socket_server_release(struct socket_server *ss);



int socket_server_poll(struct socket_server *, struct socket_message *result, int *more);

void socket_server_exit(struct socket_server *ss);
void socket_server_close(struct socket_server *ss,uintptr_t opaque,int id);

void socket_server_start(struct socket_server *ss,uintptr_t opaque,int id);


int64_t socket_server_send(struct socket_server *ss, int id,const void *buffer,int sz);
void socket_server_send_lowpriority(struct socket_server *ss, int id, const void * buffer, int sz);

int socket_server_listen(struct socket_server *ss,uintptr_t opaque,const char *addr,int port,int backlog);

int socket_server_connect(struct socket_server *ss,uintptr_t opaque,const char * addr, int port);

int socket_server_bind(struct socket_server *ss ,uintptr_t opaque, int fd);

void socket_server_nodelay(struct socket_server *ss,int id);

struct socket_udp_address;
int socket_server_udp(struct socket_server *ss ,uintptr_t opaque, const char * addr, int port);

int socket_server_udp_connect(struct socket_server *ss, int id, const char * addr, int port);

int64_t socket_server_udp_send(struct socket_server *, int id, const struct socket_udp_address *, const void *buffer, int sz);

const struct socket_udp_address *socket_server_udp_address(struct socket_server *ss ,struct socket_message *msg,int *addrsz);

struct socket_object_interface {
    void *(*buffer)(void *);
    int (*size)(void *);
    void (*free)(void *);
};

// if you send package sz == -1, use soi.
void socket_server_userobject(struct socket_server *, struct socket_object_interface *soi);
#endif /* defined(__mtask__socket_server__) */
