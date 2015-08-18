//
//  mtask_mq.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_mq__
#define __mtask__mtask_mq__

#include <stdlib.h>
#include <stdlib.h>

struct mtask_message {
    uint32_t source;   /*source of module*/
    int session;       /*session*/
    void *data;        /*user data : msg data*/
    size_t sz;         /*msg size*/
};

struct message_queue;

typedef void (*message_drop)(struct mtask_message *,void *);

void mtask_mq_init();

void mtask_globalmq_push(struct message_queue *queue);

struct message_queue * mtask_globalmq_pop(void);

struct message_queue *mtask_mq_create(uint32_t handle);

void mtask_mq_push(struct message_queue *q,struct mtask_message *message);

int mtask_mq_pop(struct message_queue *q,struct mtask_message *message);

int mtask_mq_length(struct message_queue *q);

int mtask_mq_overload(struct message_queue *q);

uint32_t mtask_mq_handle(struct message_queue *queue);

void mtask_mq_release(struct message_queue *q,message_drop drop_func,void *ud);

void mtask_mq_mark_release(struct message_queue *q);






#endif /* defined(__mtask__mtask_mq__) */
