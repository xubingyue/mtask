//
//  mtask_mq.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "mtask_mq.h"
#include "mtask_handle.h"
#include "mtask.h"


#define DEFAULT_QUEUE_SIZE 64
#define MAX_GLOBAL_MQ 0x10000   


#define MQ_IN_GLOBAL    1
#define MQ_OVERLOAD     1024


struct message_queue {
    uint32_t handle;   /*handle id of module*/
    int cap;
    int head;
    int tail;
    int lock;
    
    int release;
    int in_global;
    int overload;
    int overload_threshold;
    struct mtask_message *queue;
    struct message_queue *next;
};

struct global_queue {
    struct message_queue *head;
    struct message_queue *tail;
    int lock;
};

static struct global_queue *Q = NULL;

#define LOCK(q) while(__sync_lock_test_and_set(&(q)->lock,1)){}

#define UNLOCK(q) __sync_lock_release(&(q)->lock);




void
mtask_mq_init() {
    struct global_queue *q = mtask_malloc(sizeof(*q));
    memset(q, 0, sizeof(*q));
    Q = q;
}

void
mtask_globalmq_push(struct message_queue *queue) {
    struct global_queue *q = Q;
    
    LOCK(q);
    assert(queue->next == NULL);
    if(q->tail) {
        q->tail->next = queue;
        q->tail = queue;
    } else {
        q->head = q->tail = queue;
    }
    
    UNLOCK(q);
}


struct message_queue *
mtask_globalmq_pop(void) {
    struct global_queue *q = 0;
    
    LOCK(q);
    struct message_queue *mq = q->head;
    if(mq) {
        q->head = mq->next;
        if(q->head == NULL) {
            assert(mq == q->tail);
            q->tail = NULL;
        }
        mq->next = NULL;
    }
    
    UNLOCK(q);
    
    return mq;
}

struct message_queue *
mtask_mq_create(uint32_t handle) {
    struct message_queue *q = mtask_malloc(sizeof(*q));
    q->handle = handle;
    q->cap = DEFAULT_QUEUE_SIZE;
    q->head = 0;
    q->tail = 0;
    q->lock = 0;
    
    
    q->in_global = MQ_IN_GLOBAL;
    q->release = 0;
    q->overload = 0;
    q->overload_threshold = MQ_OVERLOAD;
    q->queue = mtask_malloc(sizeof(struct mtask_message) * q->cap);
    q->next = NULL;
    
    
    return q;
}


static void
expand_queue(struct message_queue *q) {
    struct mtask_message *new_queue = mtask_malloc(sizeof(struct mtask_message)* q->cap * 2);
    int i;
    for (i=0; i<q->cap; i++) {
        new_queue[i] = q->queue[(q->head + i) % q->cap];
    }
    q->head = 0;
    q->tail = q->cap;
    q->cap *= 2;
    
    mtask_free(q->queue);
    q->queue = new_queue;
}

void
mtask_mq_push(struct message_queue *q,struct mtask_message *message) {
    assert(message);
    LOCK(q)
    
    q->queue[q->tail] = *message;
    if(++ q->tail >= q->cap) {
        q->tail = 0;
    }
    
    if(q->head == q->tail) {
        expand_queue(q);
    }
    
    if(q->in_global == 0) {
        q->in_global = MQ_IN_GLOBAL;
        mtask_globalmq_push(q);
    }
    
    UNLOCK(q)
}
/*In the services queue (per client mq) team head, POP out the MSG */
/* 0 ==  success ; 1 = fail*/
int
mtask_mq_pop(struct message_queue *q,struct mtask_message *message) {
    int ret = 1;
    LOCK(q);
    
    if(q->head != q->tail) {
        *message = q->queue[q->head++];
        ret = 0;
        int head = q->head;
        int tail = q->tail;
        int cap = q->cap;
        
        if(head >= cap) {
            q->head = head = 0;
        }
        
        int length = tail - head;
        if(length < 0) {
            length += cap;
        }
        while (length > q->overload_threshold) {
            q->overload = length;
            q->overload_threshold *= 2;

        }
    } else {
        q->overload_threshold = MQ_OVERLOAD;
    }
    
    if(ret) {
        q->in_global = 0;
    }

    UNLOCK(q)
    
    return ret;
}


int
mtask_mq_length(struct message_queue *q) {
    int head,tail,cap;
    
    LOCK(q)
    head = q->head;
    tail = q->tail;
    cap  = q->cap;
    
    UNLOCK(q)
    
    if(head <= tail) {
        return tail - head;
    }
    return tail + cap -head;
}
/*Check whether the message_queue is load */
int
mtask_mq_overload(struct message_queue *q) {
    if(q->overload) {
        int overload = q->overload;
        q->overload = 0;
        return overload;
    }
    return 0;
}
/*get handle of module in message_queue*/
uint32_t
mtask_mq_handle(struct message_queue *q) {
    return q->handle;
}

static void
_release(struct message_queue *q) {
    assert(q->next == NULL);
    mtask_free(q->queue);
    mtask_free(q);
}


static void
_drop_queue(struct message_queue *q,message_drop drop_func,void *ud) {
    struct mtask_message msg;
    while (!mtask_mq_pop) {
        drop_func(&msg,ud);
    }
    _release(q);
}

void
mtask_mq_release(struct message_queue *q,message_drop drop_func,void *ud) {
    LOCK(q);
    
    if(q->release) {
        UNLOCK(q);
        _drop_queue(q, drop_func, ud);
    } else {
        mtask_globalmq_push(q);
        UNLOCK(q);
    }
}


void
mtask_mq_mark_release(struct message_queue *q) {
    LOCK(q)
    assert(q->release == 0);
    q->release = 1;
    if(q->in_global != MQ_IN_GLOBAL) {
        mtask_globalmq_push(q);
    }
    
    UNLOCK(q)
}

















