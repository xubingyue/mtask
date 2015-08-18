//
//  mtask_server.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#ifndef __mtask__mtask_server__
#define __mtask__mtask_server__

#include <stdint.h>
#include <stdlib.h>

struct mtask_context;

struct mtask_message;

struct mtask_monitor;

struct mtask_context *mtask_context_new(const char *name,const char *parm) ;

uint32_t mtask_context_handle(struct mtask_context * ctx);

struct mtask_context * mtask_context_release(struct mtask_context *ctx);



int mtask_context_newsession(struct mtask_context *ctx);

void mtask_context_dispatchall(struct mtask_context *ctx) ;

int mtask_context_push(uint32_t handle,struct mtask_message *message);

void mtask_context_grab(struct mtask_context *ctx);

void mtask_context_endless(uint32_t handle);

int mtask_context_total();

struct message_queue *mtask_context_message_dispatch(struct mtask_monitor *m,struct message_queue *message,int weight);

void mtask_context_send(struct mtask_context *ctx,void *msg,size_t sz,uint32_t source,int type,int session);

void mtask_context_reserve(struct mtask_context *ctx);

void mtask_global_init(void);

void mtask_global_exit(void);

void mtask_initthread(int m);





#endif /* defined(__mtask__mtask_server__) */
