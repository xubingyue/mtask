//
//  mtask_gate.c
//  mtask
//
//  Created by TTc on 14/8/7.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>


#include "mtask.h"
#include "hashid.h"
#include "databuffer.h"
#include "mtask_socket.h"

#define BACKLOG 32

struct connection {
    int id;
    uint32_t agent;
    uint32_t client;
    char remote_name[32];
    struct databuffer buffer;
};


struct gate {
    struct mtask_context *ctx;
    int listen_id;
    uint32_t watchdog;
    uint32_t broker;
    int client_tag;
    int header_size;
    int max_connection;

    struct hashid hash;
    struct connection *conn;
    
    struct messagepool mp; /*todo: save message pool ptr for release*/
};

struct gate *
gate_create(void) {
    struct gate *g = mtask_malloc(sizeof(*g));
    memset(g, 0, sizeof(*g));
    g->listen_id = -1;
    return g;
}

static void
_parm(char *msg,int sz,int command_sz) {
    while (command_sz <sz) {
        if (msg[command_sz != ' ']) {
            break;
        }
        ++command_sz;
    }
    int i;
    for (i=command_sz; i<sz; i++) {
        msg[i-command_sz] = msg[i];
    }
    msg[i-command_sz] = '\0';
}

static void
_forward_agent(struct gate *g,int fd,int32_t agentaddr,uint32_t clientaddr) {
    int id = hashid_lookup(&g->hash, fd);
    if (id>=0) {
        struct connection *agent = &g->conn[id];
        agent->agent = agentaddr;
        agent->client = clientaddr;
    }
}

static void
_ctrl(struct gate *g,const void *msg,int sz) {
    struct mtask_context *ctx = g->ctx;
    char tmp[sz +1];
    memcpy(tmp, msg, sz);
    tmp[sz]='\0';
    
    char *command = tmp;
    int i;
    if (sz ==0) {
        return;
    }
    
    for (i=0; i<sz; i++) {
        if (command[i]==' ') {
            break;
        }
    }
    
    if (memcmp(command, "kick", i) ==0) {
        _parm(tmp,sz,i);
        int uid = strtol(command, NULL, 10);
        int id = hashid_lookup(&g->hash, uid);
        if (id>=0) {
            mtask_socket_close(ctx, uid);
        }
        return;
    }
    
    if (memcmp(command, "forward", i)==0) {
        _parm(tmp, sz, i);
        
        char *client = tmp;
        char *idstr = strsep(&client, " ");
        if (client == NULL) {
            return;
        }
        
        int id = strtol(idstr, NULL, 10);
        char *agent = strsep(&client, " ");
        if (client==NULL) {
            return;
        }
        uint32_t agent_handle = strtoul(agent+1, NULL, 16);
        uint32_t client_handle = strtoul(client+1, NULL, 16);
        _forward_agent(g, idstr, agent_handle, client_handle);
        return;
    }
    
    if (memcmp(command, "broker", i)==0) {
        _parm(tmp, sz, i);
        g->broker = mtask_queryname(ctx, command);
        return;
    }
    
    if (memcmp(command, "start", i) ==0) {
        mtask_socket_start(ctx, g->listen_id);
        return;
    }
    
    if (memcmp(command, "close", i)==0) {
        if (g->listen_id>=0) {
            mtask_socket_close(ctx, g->listen_id);
            g->listen_id = -1;
        }
        return;
    }
    mtask_error(ctx, "[gate] Unknow command : %s",command);
}

static void
_forward(struct gate *g ,struct connection *c, int size) {
    struct mtask_context *ctx = g->ctx;
    if (g->broker) {
        void *tmp = mtask_malloc(size);
        databuffer_read(&c->buffer, &g->mp, tmp, size);
        mtask_send(ctx, 0, g->broker, g->client_tag | PTYPE_TAG_DONT_COPY, 0, tmp, size);
        return;
    }
    
    if (c->agent) {
        void *tmp = mtask_malloc(size);
        mtask_send(ctx, c->client, c->agent, g->client_tag | PTYPE_TAG_DONT_COPY, 0, tmp, size);
    } else if(g->watchdog) {
        char *tmp = mtask_malloc(size+32);
        int n = snprintf(tmp, 32, "%d data", c->id);
        databuffer_read(&c->buffer, &g->mp, tmp+n, size);
        mtask_send(ctx, 0, g->watchdog, PTYPE_TEXT | PTYPE_TAG_DONT_COPY, 0, tmp, size);
    }
    
}

static void
dispatch_message(struct gate *g,struct connection *c,int id ,void*data,int sz) {
    databuffer_push(&c->buffer, &g->mp, data, sz);
    for (;;) {
        int size = databuffer_readheader(&c->buffer, &g->mp, g->header_size);
        if (size<0) {
            return;
        } else if (size>0) {
            if (size>= 0x1000000) {
                struct mtask_context *ctx = g->ctx;
                databuffer_clear(&c->buffer, &g->mp);
                mtask_socket_close(ctx, id);
                mtask_error(ctx, "Recv socket message > 16M");
                return;
            } else {
                _forward(g, c, size);
                databuffer_reset(&c->buffer);
            }
        }
        
    }
}

static void
_report(struct gate*g,const char *data,...) {
    if (g->watchdog==0) {
        return;
    }
    struct mtask_context *ctx = g->ctx;
    va_list ap;
    va_start(ap, data);

    char tmp[1024];
    int n= vsnprintf(tmp, sizeof(tmp), data , ap);
    va_end(ap);
    
    mtask_send(ctx, 0, g->watchdog, PTYPE_TEXT, 0, tmp, n);
}

static void
dispatch_socket_message(struct gate *g,const struct mtask_socket_message *message,int sz) {
    struct mtask_context *ctx = g->ctx;
    switch (message->type) {
        case MTASK_SOCKET_TYPE_DATA: {
            int id = hashid_lookup(&g->hash, message->id);
            if (id>=0) {
                struct connection *c = &g->conn[id];
                dispatch_message(g, c, message->id, message->buffer, sz);
            } else {
                mtask_error(ctx, "Drop unknow connection %d message",message->id);
                mtask_socket_close(ctx, message->id);
                mtask_free(message->buffer);
            }
            break;
        }
            
        case MTASK_SOCKET_TYPE_CONNECT: {
            if (message->id == g->listen_id) {
                /*start listening*/
                break;
            }
            int id = hashid_lookup(&g->hash, message->id);
            if (id>=0) {
                struct connection *c = &g->conn[id];
                _report(g, "%d open %d %s:0",message->id,message->id,c->remote_name);
            } else {
                mtask_error(ctx, "Close unknow connection %d",message->id);
                mtask_socket_close(ctx, message->id);
            }
            break;
        }
        
        case MTASK_SOCKET_TYPE_CLOSE:
        case MTASK_SOCKET_TYPE_ERROR: {
            int id = hashid_remove(&g->hash, message->id);
            if (id>=0) {
                struct connection *c = &g->conn[id];
                databuffer_clear(&c->buffer, &g->mp);
                memset(c, 0, sizeof(*c));
                c->id = -1;
                _report(g, "%d close",message->id);
            }
            break;
        }
        case MTASK_SOCKET_TYPE_ACCEPT:
            /*report accept ,then it will be get a MTASK_SOCKET_TYPE_CONNECT message*/
            assert(g->listen_id == message->id);
            if (hashid_full(&g->hash)) {
                mtask_socket_close(ctx, message->ud);
            } else {
                struct connection *c = &g->conn[hashid_insert(&g->hash, message->id)];
                if (sz>= sizeof(c->remote_name)) {
                    sz=sizeof(c->remote_name) -1;
                }
                c->id = message->ud;
                memcpy(c->remote_name, message+1, sz);
                c->remote_name[sz] = '\0';
                mtask_socket_start(ctx, message->ud);
            }
            break;
    }
}

static int
_gate(struct mtask_context * ctx, void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    struct gate *g = ud;
    switch(type) {
        case PTYPE_TEXT:
            _ctrl(g, msg, (int)sz);
            break;
        case PTYPE_CLIENT:{
            if (sz <=4) {
                mtask_error(ctx, "Invalid client message from %x",source);
                break;
            }
           /*the last 4bytes in msg are the id of socket ,write following bytes to it*/
            const uint8_t *idbuf =msg +sz-4;
            uint32_t uid = idbuf[0] | idbuf[1]<< 8 | idbuf[2] << 16 | idbuf[3]<< 32;
            
            int id = hashid_lookup(&g->hash, uid);
            if (id>=0) {
                /*don't send id (last 4bytes)*/
                mtask_socket_send(ctx, uid, (void*)msg, sz-4);
                /*return 1 means dont free msg*/
                return 1;
            } else {
                mtask_error(ctx, "Invalid client id %d from %x",(int)uid,source);
                break;
            }
            break;
        }
        case PTYPE_SOCKET:
            assert(source==0);
            /*recv  socket message from mtast_socket*/
            dispatch_socket_message(g, msg, (int)(sz-sizeof(struct mtask_socket_message)));
    }
    return 0;
}

static int
start_listen(struct gate *g ,char *listen_addr) {
    struct mtask_context *ctx = g->ctx;
    char *portstr = strchr(listen_addr, ':');
    const char *host = "";
    
    int port;
    if (portstr==NULL) {
        port = strtol(listen_addr, NULL, 10);
        if (port<=0) {
            mtask_error(ctx, "Invalid gate address %s",listen_addr);
            return 1;
        }
    } else {
        port = strtol(portstr+1, NULL, 10);
        if (port <=0) {
            mtask_error(ctx, "Invalid gate address %s",listen_addr);
            return 1;
        }
        portstr[0] = '\0';
        host = listen_addr;
    }
    g->listen_id = mtask_socket_listen(ctx, host, port, BACKLOG);
    if (g->listen_id <0) {
        return 1;
    }
    return 0;
}

int
gate_init(struct gate *g,struct mtask_context *ctx,const char *parm) {
    if (parm==NULL) {
        return 1;
    }
    int max =0;
    int buffer =0;
    
    int sz = strlen(parm)+1;
    
    char watchdog[sz];
    char binding[sz];
    int client_tag =0;
    char header;
    
    
    int n = sscanf(parm, "%c %s %s %d %d %d",&header,watchdog, binding,&client_tag , &max,&buffer);
    if (n<4) {
        mtask_error(ctx, "Invalid gate parm  %s",parm);
        return 1;
    }
    
    if (max<=0) {
        mtask_error(ctx, "Need max connection");
        return 1;
    }
    if (header != 'S' && header != 'L') {
        mtask_error(ctx, "Invalid data header style");
        return 1;
    }
    if (client_tag==0) {
        client_tag=PTYPE_CLIENT;
    }
    if (watchdog[0]=='!') {
        g->watchdog =0;
    } else {
        g->watchdog= mtask_queryname(ctx, watchdog);
        if (g->watchdog==0) {
            mtask_error(ctx, "Invalid watchdog %s",watchdog);
            return 1;
        }
    }
    
    g->ctx = ctx;
    
    hashid_init(&g->hash, max);
    g->conn = mtask_malloc(max *sizeof(struct connection));
    memset(g->conn, 0, max*sizeof(struct connection));
    g->max_connection = max;
    int i;
    for (i=0; i<max; i++) {
        g->conn[i].id = -1;
    }
    g->client_tag = client_tag;
    g->header_size = header == 'S' ? 2:4;
    mtask_callback(ctx, g, _gate);
    
    return start_listen(g,binding);
}














