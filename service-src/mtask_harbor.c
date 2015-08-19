//
//  mtask_harbor.c
//  mtask
//
//  Created by TTc on 14/8/6.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
//远程消息代理模块,它只负责管理 tcp 连接的 fd, 而不必操心连接连接的过程。这样，也不再依赖额外的 gate 服务，只需要做简单的拼包处理即可
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "mtask.h"
#include "mtask_harbor.h"
#include "mtask_socket.h"


#define HASH_SIZE 4096
#define DEFAULT_QUEUE_SIZE 1024
// 12 is sizeof(struct remote_message_header)
#define HEADER_COOKIE_LENGTH 12
/* 
	message type (8bits) is in destination high 8bits
	harbor id (8bits) is also in that place , but remote message doesn't need harbor id.
*/
struct remote_message_header {
    uint32_t source;
    uint32_t destination;
    uint32_t session;
};

struct harbor_msg {
    struct remote_message_header header;
    void *buffer;
    size_t size;
};

struct harbor_msg_queue {
    int size;
    int head;
    int tail;
    struct harbor_msg *data;
};


struct keyvalue {
    struct keyvalue *next;
    char key[GLOBALNAME_LENGTH];
    uint32_t hash;
    uint32_t value;
    struct harbor_msg_queue *queue;
};

struct hashmap {
    struct keyvalue *node[HASH_SIZE];
};

#define STATUS_WAIT      0
#define STATUS_HANDSHAKE 1
#define STATUS_HEADER    2
#define STATUS_CONNECT   3
#define STATUS_DOWN      4

struct slave {
    int fd;
    struct harbor_msg_queue *queue;
    int status;
    int length;
    int read;
    uint8_t size[4];
    char *recv_buffer;
};


struct harbor {
    struct mtask_context *ctx;
    int id;
    uint32_t slave;
    struct hashmap *map;
    struct slave s[REMOTE_MAX];
};

static struct hashmap *
hash_new() {
    struct hashmap *h = mtask_malloc(sizeof(struct hashmap));
    memset(h, 0, sizeof(*h));
    return h;
}

static struct keyvalue *
hash_insert(struct hashmap *hash,const char name[GLOBALNAME_LENGTH]) {
    uint32_t *ptr = (uint32_t *)name;
    uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ptr[3];

    struct keyvalue **pkv = &hash->node[h%HASH_SIZE];
    struct keyvalue * node = mtask_malloc(sizeof(*node));
    
    memcpy(node->key, name, GLOBALNAME_LENGTH);
    node->next = *pkv;
    node->queue = NULL;
    node->hash = h;
    node->value = 0;
    *pkv = node;
    
    return node;
}

static struct keyvalue *
hash_search(struct hashmap *hash,const char name[GLOBALNAME_LENGTH]) {
    uint32_t *ptr = (uint32_t *)name;
    uint32_t h = ptr[0] ^ ptr[1] ^ ptr[2] ^ ptr[3];
    struct keyvalue *node =hash->node[h%HASH_SIZE];
    
    while (node) {
        if (node->hash == h && strncmp(node->key, name, GLOBALNAME_LENGTH) == 0) {
            return node;
        }
        node = node->next;
    }
    return NULL;
}


struct harbor *
harbor_create(void) {
    struct harbor *h = mtask_malloc(sizeof(*h));
    memset(h, 0, sizeof(*h));
    h->map = hash_new();
    return h;
}


static struct harbor_msg_queue *
new_queue() {
    struct harbor_msg_queue *queue = mtask_malloc(sizeof(*queue));
    queue->size = DEFAULT_QUEUE_SIZE;
    queue->head =0;
    queue->tail =0;
    queue->data = mtask_malloc(DEFAULT_QUEUE_SIZE * sizeof(struct harbor_msg));
    return queue;
}

static struct harbor_msg *
pop_queue(struct harbor_msg_queue *queue) {
    if (queue->head == queue->tail) {
        return NULL;
    }
    struct harbor_msg *slot = &queue->data[queue->head];
    queue->head = (queue->head + 1) % queue->size;
    return slot;
}

static inline void
to_bigendian(uint8_t *buffer,uint32_t n) {
    buffer[0] = (n>>24) & 0xff;
    buffer[1] = (n>>16) & 0xff;
    buffer[2] = (n>>8)  & 0xff;
    buffer[3] = n & 0xff;
}

static inline void
header_to_message(const struct remote_message_header *header, uint8_t *message) {
    to_bigendian(message, header->source);
    to_bigendian(message+4, header->destination);
    to_bigendian(message+8, header->source);
}

static void
release_queue(struct harbor_msg_queue *queue) {
    if (queue == NULL) {
        return;
    }
    struct harbor_msg *m;
    while ((m=pop_queue(queue))) {
        mtask_free(m->buffer);
    }
    mtask_free(queue->data);
    mtask_free(queue);
}

static void
close_harbor(struct harbor *h,int id) {
    struct slave *s = &h->s[id];
    s->status = STATUS_DOWN;
    if (s->fd) {
        mtask_socket_close(h->ctx, s->fd);
    }
    if (s->queue) {
        release_queue(s->queue);
        s->queue = NULL;
    }
}

static void
hash_delete(struct hashmap *hash) {
    int i;
    for (i=0; i<HASH_SIZE; i++) {
        struct keyvalue *node = hash->node[i];
        while (node) {
            struct keyvalue *next = node->next;
            release_queue(node->queue);
            mtask_free(node);
            node = next;
        }
    }
    mtask_free(hash);
}

void
harbor_release(struct harbor *h) {
    int i;
    for (i=0; i<REMOTE_MAX; i++) {
        struct slave *s = &h->s[i];
        
        if (s->fd && s->status != STATUS_DOWN) {
            /*don't call report_harbor_down.  never call mtask_send during module exit. beacuse of dead lock*/
            close_harbor(h,i);
        }
    }
    hash_delete(h->map);
    mtask_free(h);
}

static void
send_remote(struct mtask_context *ctx,int fd,const char *buffer,size_t sz,struct remote_message_header *cookie) {
    uint32_t sz_header = sz + sizeof(*cookie);
    uint8_t *sendbuf = mtask_malloc(sz_header + 4);
    to_bigendian(sendbuf,sz_header);
    memcpy(sendbuf+4, buffer, sz);
    
    header_to_message(cookie,sendbuf+4+sz);
    
    /*ignore send error .because if the connection is broken,the mainloop will recv a message.*/
    mtask_socket_send(ctx, fd, sendbuf, sz_header+4);
}

static void
dispatch_queue(struct harbor *h,int id) {
    struct slave *s = &h->s[id];
    int fd = s->fd;
    assert(fd != 0);
    
    struct harbor_msg_queue *queue = s->queue;
    if (queue == NULL) {
        return;
    }
    
    struct harbor_msg *m;
    while ((m=pop_queue(queue)) != NULL) {
        send_remote(h->ctx, fd, m->buffer, m->size, &m->header);
        mtask_free(queue);
    }
    release_queue(queue);
    s->queue = NULL;
}

static inline uint32_t
from_big_endian(uint32_t n) {
    union{
        uint32_t big;
        uint8_t bytes[4];
    }u;
    u.big =n;
    return u.bytes[0] << 24| u.bytes[1] << 16 | u.bytes[2]<< 8 | u.bytes[3];
}

static inline void
message_to_header(const uint32_t *message,struct remote_message_header *header) {
    header->source = from_big_endian(message[0]);
    header->destination = from_big_endian(message[1]);
    header->session = from_big_endian(message[2]);
}

static void
forward_local_message(struct harbor *h,void *msg,int sz) {
    const char *cookie = msg;
    cookie += sz - HEADER_COOKIE_LENGTH;
    struct remote_message_header header;
    message_to_header((const uint32_t*)cookie, &header);
    
    uint32_t dst = header.destination;
    int type = (dst >> HANDLE_REMOTE_SHIFT) | PTYPE_TAG_DONT_COPY;
    dst = (dst & HANDLE_MASK) | ((uint32_t)h->id << HANDLE_REMOTE_SHIFT);
    
    int size = sz - HEADER_COOKIE_LENGTH;
    if (mtask_send(h->ctx, header.source, dst, type, (int)header.session,msg,size) < 0) {
        /*harbor can post error message back when the destination is not exist : fixed*/
        mtask_send(h->ctx, dst, header.source , PTYPE_ERROR, (int)header.session, NULL, 0);
        mtask_error(h->ctx, "Unknown destination :%x from :%x", dst, header.source);
    }
}

static void
push_socket_data(struct harbor *h,const struct mtask_socket_message *message) {
    assert(message->type == MTASK_SOCKET_TYPE_DATA);
    int fd = message->id;
    int i;
    int id = 0;
    struct slave *s = NULL;
    for (i=1; i<REMOTE_MAX; i++) {
        if (h->s[i].fd == fd) {
            s= &h->s[i];
            id=1;
            break;
        }
    }
    
    if (s==NULL) {
        mtask_free(message->buffer);
        mtask_error(h->ctx, "Invalid socket fd (%d) data", fd);
        return;
    }
    
    uint8_t *buffer = (uint8_t *)message->buffer;
    int size = message->ud;
    
    for (;;) {
        switch (s->status) {
            case STATUS_HANDSHAKE: {
                //check id
                uint8_t remote_id = buffer[0];
                if (remote_id != id) {
                    mtask_error(h->ctx, "Invalid shakehand id (%d) from fd = %d , harbor = %d", id, fd, remote_id);
                    close_harbor(h, id);
                    return;
                }
                ++buffer;
                --size;
                s->status = STATUS_HEADER;
                
                dispatch_queue(h, id);
                
                if (size == 0) {
                    break;
                }
                //go though
            }
                
            case STATUS_HEADER: {
                /*big endian 4 bytes length,the first one must be 0*/
                int need = 4 - s->read;
                if (size < need) {
                    memcpy(s->size + s->read, buffer, size);
                    s->read += size;
                    return;
                } else {
                    memcpy(s->size + s->read, buffer, need);
                    buffer += need;
                    size -= need;
                    
                    if (s->size[0] != 0) {
                        mtask_error(h->ctx, "Message is too long from harbor %d", id);
                        close_harbor(h,id);
                        return;
                    }
                    s->length = s->size[1] << 16 | s->size[2] << 8 | s->size[3];
                    s->read = 0;
                    s->recv_buffer =mtask_malloc(s->length);
                    s->status = STATUS_CONNECT;
                    if (size ==0) {
                        return;
                    }
                }
            }
            case STATUS_CONNECT: {
                int need = s->length - s->read;
                if (size < need) {
                    memcpy(s->recv_buffer+ s->read, buffer,size);
                    s->read += size;
                    return;
                }
                memcpy(s->recv_buffer+s->read, buffer, need);
                
                forward_local_message(h, s->recv_buffer, s->length);
                s->length = 0;
                s->read =0;
                s->recv_buffer = NULL;
                size -= need;
                buffer += need;
                s->status = STATUS_HEADER;
                if (size ==0) {
                    return;
                }
                
            }
                
            default:
                break;
        }
    }
}



//hash table
static void
push_queue_msg(struct harbor_msg_queue *queue,struct harbor_msg *m) {
    if (((queue->tail +1) % queue->size) == queue->head) {
        struct harbor_msg *new_buffer = mtask_malloc(sizeof(queue->size * 2 * sizeof(struct harbor_msg)));
        int i;
        for (i=0; i<queue->size-1; i++) {
            new_buffer[i] = queue->data[(i+queue->head) % queue->size];
        }
        mtask_free(queue->data);
        queue->data = new_buffer;
        queue->head = 0;
        queue->tail = queue->size -1;
        queue->size *= 2;
    }
    
    struct harbor_msg *slot = &queue->data[queue->tail];
    *slot = *m;
    queue->tail = (queue->tail +1) % queue->size;
}

static void
push_queue(struct harbor_msg_queue * queue, void * buffer, size_t sz, struct remote_message_header * header) {
    struct harbor_msg m;
    m.header = *header;
    m.buffer = buffer;
    m.size = sz;
    push_queue_msg(queue, &m);
}

static int
harbor_id(struct harbor *h,int fd) {
    int i ;
    for (i=0; i<REMOTE_MAX; i++) {
        struct slave *s = &h->s[i];
        if (s->fd == fd) {
            return i;
        }
    }
    return 0;
}

static void
report_harbor_down(struct harbor *h,int id) {
    char down[64];
    int n = sprintf(down, "D %d",id);
    
    mtask_send(h->ctx, 0, h->slave, PTYPE_TEXT, 0, down, n);
}

static void
dispatch_name_queue(struct harbor *h,struct keyvalue *node) {
    struct harbor_msg_queue *queue = node->queue;
    uint32_t handle = node->value;
    int harbor_id = handle >> HANDLE_REMOTE_SHIFT;
    assert(harbor_id != 0);
    struct mtask_context *ctx = h->ctx;
    struct slave *s = &h->s[harbor_id];
    int fd = s->fd;
    if (fd == 0) {
        if (s->status == STATUS_DOWN) {
            char tmp[GLOBALNAME_LENGTH +1];
            memcpy(tmp, node->key, GLOBALNAME_LENGTH);
            tmp[GLOBALNAME_LENGTH] = '\0';
            mtask_error(ctx, "Drop message to %s (in harbor %d)",tmp,harbor_id);
        } else {
            if (s->queue == NULL) {
                s->queue = node->queue;
                node->queue = NULL;
            } else {
                struct harbor_msg *m;
                while ((m=pop_queue(queue)) != NULL) {
                    push_queue_msg(s->queue, m);
                }
            }
        }
        return;
    }
    struct harbor_msg *m;
    while ((m=pop_queue(queue)) != NULL) {
        m->header.destination |= (handle & HANDLE_MASK);
        send_remote(ctx, fd, m->buffer, m->size, &m->header);
        mtask_free(m->buffer);
    }
}

static void
update_name(struct harbor *h,const char name[GLOBALNAME_LENGTH],uint32_t handle) {
    struct keyvalue * node = hash_search(h->map,name);
    if (node == NULL) {
        node = hash_insert(h->map,name);
    }
    
    node->value = handle;
    if (node->queue) {
        dispatch_name_queue(h, node);
        release_queue(node->queue);
        node->queue = NULL;
    }
}

static void
handshake(struct harbor *h,int id) {
    struct slave *s = &h->s[id];
    uint8_t *handshake = mtask_malloc(1);
    handshake[0] = (uint8_t)h->id;
    mtask_socket_send(h->ctx, s->fd, handshake, 1);
}

static void
harbor_command(struct harbor *h ,const char *msg,size_t sz, int session, uint32_t source) {
    const char *name = msg + 2;
    int s = (int)sz;
    s -= 2;
    switch (msg[0]) {
        case 'N': {
            if (s <= 0 || s>=GLOBALNAME_LENGTH) {
                mtask_error(h->ctx, "Invalid global name %s", name);
                return;
            }
            struct remote_name rn;
            memset(&rn, 0, sizeof(rn));
            memcpy(rn.name, name, s);
            rn.handle = source;
            
            update_name(h, rn.name, rn.handle);
            
            break;
        }
            
        case 'S':
        case 'A': {
            char buffer[s+1];
            memcpy(buffer, name, s);
            buffer[s] = 0;
            int fd=0;
            int id=0;
            sscanf(buffer, "%d  %d",&fd,&id);
            if (fd ==0 || id<= 0 ||id>=REMOTE_MAX) {
                mtask_error(h->ctx, "Invalid command %c %s",msg[0],buffer);
                return;
            }
            struct slave *slave = &h->s[id];
            if (slave->fd != 0) {
                mtask_error(h->ctx, "Harbor %d already exist",id);
                return;
            }
            slave->fd = fd;
            
            mtask_socket_start(h->ctx, fd);
            handshake(h, fd);
        
            if (msg[0] == 'S') {
                slave->status = STATUS_HANDSHAKE;
            } else {
                slave->status = STATUS_HEADER;
                dispatch_queue(h, id);
            }
            break;
        }
        default:
            mtask_error(h->ctx, "Unknown command %s",msg);
            break;
    }
}

static int
remote_send_handle(struct harbor *h,uint32_t source ,uint32_t dst,int type,int session,
                   const char *msg, size_t sz) {
    int harbor_id = dst >> HANDLE_REMOTE_SHIFT;
    struct mtask_context *ctx = h->ctx;
    if (harbor_id == h->id) {
        /*local message*/
        mtask_send(ctx, source, dst, type | PTYPE_TAG_DONT_COPY, session, (void*)msg, sz);
        return 1;
    }
    
    struct slave *s = &h->s[harbor_id];
    if (s->fd == 0 || s->status == STATUS_HANDSHAKE) {
        if (s->status == STATUS_DOWN) {
            /*throw an error return to source,report the dst is dead*/
            mtask_send(ctx, source, dst, PTYPE_ERROR, 0, NULL, 0);
            mtask_error(ctx, "Drop message to harbor %d from %x to %x (session = %d, msgsz = %d)",harbor_id, source, dst,session,(int)sz);
        } else {
            if (s->queue == NULL) {
                s->queue = new_queue();
            }
            struct remote_message_header header;
            header.source = source;
            header.destination = (type << HANDLE_REMOTE_SHIFT);
            header.session = (uint32_t)session;
            
            push_queue(s->queue, (void*)msg, sz, &header);
            
            return 1;
        }
    } else {
        struct remote_message_header cookie;
        cookie.source = source;
        cookie.destination = (dst & HANDLE_MASK) | ((uint32_t)type << HANDLE_REMOTE_SHIFT);
        cookie.session =(uint32_t)session;
        
        send_remote(ctx, s->fd, msg, sz, &cookie);
    }
    return 0;
}

static int
remote_send_name(struct harbor *h,uint32_t source,const char name[GLOBALNAME_LENGTH], int type, int session, const char * msg, size_t sz) {
    struct keyvalue *node = hash_search(h->map,name);
    if (node == NULL) {
        node = hash_insert(h->map,name);
    }
    
    if (node->value == 0) {
        if (node->queue == NULL) {
            node->queue = new_queue();
        }
        struct remote_message_header header;
        header.source = source;
        header.destination = type << HANDLE_REMOTE_SHIFT;
        header.session = (uint32_t)session;
        
        push_queue(node->queue, (void*)msg, sz, &header);
        char query[2+GLOBALNAME_LENGTH+1] = "Q ";
        query[2+GLOBALNAME_LENGTH] = 0;
        memcpy(query+2, name, GLOBALNAME_LENGTH);
        mtask_send(h->ctx, 0, h->slave, PTYPE_TEXT, 0, query, strlen(query));
        return 1;
    } else {
        return remote_send_handle(h, source, node->value, type, session, msg, sz);
    }
}

static int
mainloop(struct mtask_context * context, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    struct harbor * h = ud;
    switch (type) {
        case PTYPE_SOCKET: {
            const struct mtask_socket_message * message = msg;
            switch(message->type) {
                case MTASK_SOCKET_TYPE_DATA:
                    push_socket_data(h, message);
                    mtask_free(message->buffer);
                    break;
                case MTASK_SOCKET_TYPE_ERROR:
                case MTASK_SOCKET_TYPE_CLOSE: {
                    int id = harbor_id(h, message->id);
                    if (id) {
                        report_harbor_down(h,id);
                    } else {
                        mtask_error(context, "Unkown fd (%d) closed", message->id);
                    }
                    break;
                }
                case MTASK_SOCKET_TYPE_CONNECT:
                    // fd forward to this service
                    break;
                default:
                    mtask_error(context, "recv invalid socket message type %d", type);
                    break;
            }
            return 0;
        }
        case PTYPE_HARBOR: {
            harbor_command(h, msg,sz,session,source);
            return 0;
        }
        default: {
            // remote message out
            const struct remote_message *rmsg = msg;
            if (rmsg->destination.handle == 0) {
                if (remote_send_name(h, source , rmsg->destination.name, type, session, rmsg->message, rmsg->sz)) {
                    return 0;
                }
            } else {
                if (remote_send_handle(h, source , rmsg->destination.handle, type, session, rmsg->message, rmsg->sz)) {
                    return 0;
                }
            }
            mtask_free((void *)rmsg->message);
            return 0;
        }
    }
}

int
harbor_init(struct harbor *h,struct mtask_context *ctx,const char *args) {
    h->ctx = ctx;
    int harbor_id =0;
    uint32_t slave = 0;
    sscanf(args, "%d %u",&harbor_id,&slave);
    if (slave == 0) {
        return 1;
    }
    
    h->id = harbor_id;
    h->slave = slave;
    mtask_callback(ctx, h, mainloop);
    mtask_harbor_start(ctx);
    
    return 0;
}







