//
//  mtask_socket.c
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "mtask.h"
#include "mtask_socket.h"
#include "mtask_server.h"
#include "mtask_mq.h"
#include "mtask_harbor.h"

#include "socket_server.h"


static struct socket_server *SOCKET_SERVER = NULL;

static int
check_wsz(struct mtask_context *ctx,int id, void *buffer,int64_t wsz) {
    if (wsz<0) {
        return -1;
    }else if (wsz > 1024 * 1024) {
        int kb4 = wsz / 1024 /4;
        if (kb4 % 256 == 0) {
            mtask_error(ctx, "%d MB bytes on socket %d need to send out.\n",(int)(wsz / (1024 * 1024)), id);
        }
    }
    return 0;
}

void
mtask_socket_init() {
    SOCKET_SERVER = socket_server_create();
}

void
mtask_socket_exit() {
    socket_server_exit(SOCKET_SERVER);
}

int
mtask_socket_poll() {
    struct socket_server *ss  = SOCKET_SERVER;
    assert(ss);
    /*result used to hold return msg from the socket_server_poll*/
    struct socket_message result;
    int more = 1;
    
    /*wait event occured,and slove the event with the result msg as return*/
    int type = socket_server_poll(ss, &result, &more);
    /*forward the result msg recive*/
    switch (type) {
        case SOCKET_EXIT:
            return 0;
        case SOCKET_DATA:
            
            break;
            
        default:
            mtask_error(NULL, "Unknown socket message type %d",type);
            return -1;
    }
    /*exit when more is true*/
    if(more) return -1;
    return 1;
}
/*delete all of the socket thread*/
void
mtask_socket_free() {
    socket_server_release(SOCKET_SERVER);
    SOCKET_SERVER = NULL;
}
/*send payload high*/
int
mtask_socket_send(struct mtask_context *ctx, int id,void *buffer,int sz) {
    int64_t wsz = socket_server_send(SOCKET_SERVER, id, buffer, sz);
    return check_wsz(ctx, id, buffer, wsz);
}
/*send payoload low*/
void
mtask_socket_send_lowpriority(struct mtask_context *ctx,int id, void *buffer, int sz) {
    socket_server_send_lowpriority(SOCKET_SERVER, id, buffer, sz);
}


/*send socket close request*/
void
mtask_socket_close(struct mtask_context *ctx,int id) {
    uint32_t source = mtask_context_handle(ctx);
    socket_server_close(SOCKET_SERVER, source, id);
}

void
mtask_socket_start(struct mtask_context *ctx,int id) {
    uint32_t source = mtask_context_handle(ctx);
    socket_server_start(SOCKET_SERVER, source, id);
}

int
mtask_socket_listen(struct mtask_context *ctx,const char *host, int port, int backlog) {
    uint32_t source = mtask_context_handle(ctx);
    return socket_server_listen(SOCKET_SERVER, source, host, port, backlog);
}

/*send connect request*/
int
mtask_socket_connect(struct mtask_context *ctx,const char *host,int port) {
    /*index of the module*/
    uint32_t source = mtask_context_handle(ctx);
    /*source as the apaque*/
    return socket_server_connect(SOCKET_SERVER,source,host,port);
}
/*send bind request*/
int
mtask_socket_bind(struct mtask_context *ctx,int fd) {
    uint32_t source = mtask_context_handle(ctx);
    return socket_server_bind(SOCKET_SERVER, source, fd);
}
/*send socket nodely request*/
void
mtask_socket_nodelay(struct mtask_context *ctx,int id) {
    socket_server_nodelay(SOCKET_SERVER, id);
}

/*send udp socket request to init udp socket*/
int
mtask_socket_udp(struct mtask_context *ctx,const char * addr, int port) {
    uint32_t source = mtask_context_handle(ctx);
    return socket_server_udp(SOCKET_SERVER, source, addr, port);
}

int
mtask_socket_udp_connect(struct mtask_context *ctx,int id, const char * addr, int port) {
    return socket_server_udp_connect(SOCKET_SERVER, id, addr, port);
}

int
mtask_socket_udp_send(struct mtask_context *ctx,int id, const char * address, const void *buffer, int sz) {
    int64_t wsz = socket_server_udp_send(SOCKET_SERVER, id, (const struct socket_udp_address *)address, buffer, sz);
    return check_wsz(ctx, id, (void *)buffer, wsz);
}


const char *
mtask_socket_udp_address(struct mtask_socket_message *message,int *addrsz) {
    if (message->type != MTASK_SOCKET_TYPE_UDP) {
        return NULL;
    }
    struct socket_message sm;
    sm.id = message->id;
    sm.opaque = 0;
    sm.ud= message->ud;
    sm.data = message->buffer;
    return (const char *)socket_server_udp_address(SOCKET_SERVER, &sm, addrsz);
}