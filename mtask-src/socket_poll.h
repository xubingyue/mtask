//
//  socket_poll.h
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef mtask_socket_poll_h
#define mtask_socket_poll_h

#include <stdbool.h>



typedef int poll_fd;

struct event {
    void *s;
    bool read;
    bool write;
};

static poll_fd sp_create();

static bool sp_invalid(poll_fd fd);

static int sp_add(poll_fd fd,int sock,void *ud);

static void sp_del(poll_fd fd,int sock);

static void sp_write(poll_fd,int sock,void *ud ,bool enable);

static int sp_wait(poll_fd fd,struct event *e,int max);

static void sp_nonblocking(int sock);

static void sp_release(poll_fd fd);


//实现在下面,根据平台的不同包含不同的实现代码
#ifdef __linux__                                                //如果是linux平台
#include "socket_epoll.h"                                       //使用epoll
#endif

#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined (__NetBSD__)	//如果是apple freebsd openbsd netbsd
#include "socket_kqueue.h"                                      //使用kqueue
#endif


#endif
