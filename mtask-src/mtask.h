//
//  mtask.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef mtask_mtask_h
#define mtask_mtask_h

#include "mtask_malloc.h"


#include <stddef.h>
#include <stdint.h>


#define  PTYPE_TEXT     0
#define  PTYPE_RESPONSE  1
#define  PTYPE_MULTICAST 2
#define  PTYPE_CLIENT 3
#define  PTYPE_SYSTEM 4
#define  PTYPE_HARBOR 5
#define  PTYPE_SOCKET 6
#define  PTYPE_ERROR  7
#define  PTYPE_RESERVED_QUEUE 8
#define  PTYPE_RESERVED_DEBUG 9
#define  PTYPE_RESERVED_LUA 10
#define  PTYPE_RESERVED_SNAX 11

#define PTYPE_TAG_DONT_COPY  0x10000
#define PTYPE_TAG_ALLOC_SESSION 0x20000

struct mtask_context;

typedef int (*mtask_cb)(struct mtask_context *ctx,void *ud,int type,int session,uint32_t source,const
    void *msg,size_t sz);

void mtask_error(struct mtask_context * context, const char *msg, ...);

const char *mtask_command(struct mtask_context *ctx,const char *cmd, const char *parm);

int mtask_send_name(struct mtask_context *ctx,uint32_t source,const char *address,
                int type,int session,void *data,size_t sz);

void mtask_callback(struct mtask_context *ctx,void*ud,mtask_cb cb);

int mtask_send(struct mtask_context *ctx,uint32_t source,uint32_t dst,int type,int session,void *data,size_t sz);

uint32_t mtask_queryname(struct mtask_context *ctx,const char *name);

int mtask_isremote(struct mtask_context *ctx,uint32_t handle,int *harbor);

#endif
