//
//  mtask_harbor.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_harbor__
#define __mtask__mtask_harbor__

#include <stdlib.h>
#include <stdint.h>

#define GLOBALNAME_LENGTH  16
#define REMOTE_MAX 256

#define HANDLE_MASK 0xffffff    /*保留高8位用于远程ID*/
#define HANDLE_REMOTE_SHIFT 24


struct remote_name {
    char name[GLOBALNAME_LENGTH];
    uint32_t handle;
};


struct remote_message {
    struct remote_name destination;
    const void * message;
    size_t sz;
};


void mtask_harbor_send(struct remote_message *rmsg,uint32_t source,int session);

int mtask_harbor_message_isremote(uint32_t handle);

void mtask_harbor_init(int harbor);

void mtask_harbor_start(void *ctx);

void mtask_harbor_exit();







#endif /* defined(__mtask__mtask_harbor__) */
