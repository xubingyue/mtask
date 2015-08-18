//
//  mtask_harbor.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "mtask_harbor.h"
#include "mtask_server.h"
#include "mtask.h"


static struct mtask_context *REMOTE = NULL;
static unsigned int HARBOR = ~0; /* ~0 = 0xffffffff*/


void
mtask_harbor_init(int harbor) {
    HARBOR = (unsigned int)harbor << HANDLE_REMOTE_SHIFT; /*右移24位*/
    printf("HARBOR==%d  harbor==%d \n",HARBOR,harbor);
}

void
mtask_harbor_exit() {
    struct mtask_context *ctx = REMOTE;
    REMOTE = NULL;
    if(ctx) {
        mtask_context_release(ctx);
    }
}
/*    服务地址(32位)的高8位代表了它所属的节点,高8位即是harbor：1-255	0代表单节点模式 */
void
mtask_harbor_start(void *ctx) {
    mtask_context_reserve(ctx);
    REMOTE = ctx;
}

void
mtask_harbor_send(struct remote_message *rmsg,uint32_t source,int session) {
    int type = rmsg->sz >> HANDLE_REMOTE_SHIFT;
    rmsg->sz &= HANDLE_MASK;
    assert(type != PTYPE_SYSTEM && type != PTYPE_HARBOR);
    mtask_context_send(REMOTE, rmsg, sizeof(*rmsg), source, type, session);
}



int
mtask_harbor_message_isremote(uint32_t handle) {
    assert(HARBOR != ~0);
    int h = (handle & !HANDLE_MASK);//HADNLE_MASK = 0x00ffffff ;~HANDLE_MASK = 0xff000000 &取高8位值
    return h != HARBOR && h != 0;
}


