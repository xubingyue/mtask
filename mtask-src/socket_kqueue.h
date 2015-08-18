//
//  socket_kqueue.h
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__socket_kqueue__
#define __mtask__socket_kqueue__

#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/event.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>



static int
sp_create() {
    return kqueue();
}

static bool
sp_invalid(int kfd) {
    return kfd == -1;
}

static int
sp_add(poll_fd kfd,int sock,void *ud) {
    struct kevent ke;
    EV_SET(&ke, sock, EVFILT_READ, EV_ADD, 0, 0, ud);
    if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
        return 1;
    }
    EV_SET(&ke, sock, EVFILT_WRITE, EV_ADD, 0, 0, ud);
    if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
        EV_SET(&ke, sock, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(kfd, &ke, 1, NULL, 0, NULL);
        return 1;
    }
    EV_SET(&ke, sock, EVFILT_WRITE, EV_DISABLE, 0, 0, ud);
    if (kevent(kfd, &ke, 1, NULL, 0, NULL) == -1) {
        sp_del(kfd, sock);
        return 1;
    }
    return 0;
}


static void
sp_del(poll_fd kfd,int sock) {
    struct kevent ke;
    EV_SET(&ke,sock,EVFILT_READ,EV_DELETE,0,0,NULL);
    kevent(kfd, &ke, 1, NULL, 0, NULL);
    EV_SET(&ke, sock, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(kfd, &ke, 1, NULL, 0, NULL);
}

static void
sp_write(poll_fd kfd,int sock,void *ud ,bool enable) {
    struct kevent ke;
    EV_SET(&ke, sock, EVFILT_WRITE, enable ? EV_ENABLE : EV_DISABLE, 0, 0, ud);
    if(kevent(kfd,&ke,1,NULL,0,NULL) == -1) {
        //todo : check error
    }
}


static int
sp_wait(poll_fd kfd,struct event *e,int max) {
    struct kevent ev[max];
    int n = kevent(kfd,NULL,0,ev,max,NULL);
    
    int i;
    for (i=0;i<n;i++) {
        e[i].s = ev[i].udata;                                    //将用户数据返回
        unsigned filter = ev[i].filter;                          //取出事件
        e[i].write = (filter == EVFILT_WRITE);                   //获取是否可写
        e[i].read = (filter == EVFILT_READ);                     //获取是否可读
    }
    return n;
}

static void
sp_nonblocking(int fd) {
    int flag = fcntl(fd,F_GETFL,0);
    if(flag == -1) return;
    
    fcntl(fd,F_SETFL,flag | O_NONBLOCK);
}

static void
sp_release(int kfd) {
    close(kfd);
}

#endif /* defined(__mtask__socket_kqueue__) */
