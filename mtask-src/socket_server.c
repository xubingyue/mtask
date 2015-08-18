//
//  socket_server.c
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "mtask.h"
#include "socket_server.h"
#include "socket_poll.h"


#define MAX_INFO            128
#define MAX_EVENT           64  
#define MAX_SOCKET_P        16
#define MIN_READ_BUFFER     64

#define SOCKET_TYPE_INVALID 0
#define SOCKET_TYPE_RESERVE 1
#define SOCKET_TYPE_PLISTEN 2
#define SOCKET_TYPE_LISTEN  3
#define SOCKET_TYPE_CONNECTING 4
#define SOCKET_TYPE_CONNECTED 5
#define SOCKET_TYPE_HALFCLOSE 6
#define SOCKET_TYPE_PACCEPT   7
#define SOCKET_TYPE_BIND      8


#define MAX_SOCKET (1<<MAX_SOCKET_P)

#define PRIORITY_HIGH       0
#define PRIORITY_LOW        1

#define HASH_ID(id) (((unsigned)id) % MAX_SOCKET)                /*convert id to socket slot's index*/

#define PROTOCOL_TCP        0
#define PROTOCOL_UDP        1
#define PROTOCOL_UDPv6      2


#define UDP_ADDRESS_SIZE    19              /*16bit(2byte) + 1byte   type*/

#define MAX_UDP_PACKAGE     65535


struct write_buffer {
    struct write_buffer *next;
    void *buffer;
    char *ptr;
    int sz;
    bool userobject;
    uint8_t udp_address[UDP_ADDRESS_SIZE];
};

struct wb_list {
    struct write_buffer *head;
    struct write_buffer *tail;
};

struct socket {      /*application socket*/
    uintptr_t opaque;/*id of the module socket belong to*/
    struct wb_list high;/*high rate write buffer list*/
    struct wb_list low; /*low rate write buffer list*/
    int64_t wb_size;  /*totol size of the payload wait to send*/
    
    int fd;         /*socket fd  : kernel socket fd*/
    int id;         /*id alloc for this socket : a id of  socket pools*/
    uint16_t protocol;/*link protocol*/
    uint16_t type; /*status or type of the socket*/
 
    union {
        int size; /*tcp read buffer size*/
        uint8_t udp_address[UDP_ADDRESS_SIZE];/*udp address of the udp socket*/
    }p;
};

struct socket_server {
    int recvctrl_fd;/*read fd of pipe*/
    int sendctrl_fd;/*senf fd of pipe*/
    int checkctrl;  /*mark if check the ctrl  msg from pipe*/
    
    poll_fd event_fd;/*epoll handle event pools*/
    int alloc_id;    /*curr id alloc for socket*/
    int event_n;     /*total event occured*/
    int event_index; /*idx of the event need to solve*/
    
    struct socket_object_interface soi;/*used when need ctrl payload by user api*/
    struct event ev[MAX_EVENT];/*used to get event when call epoll*/
    struct socket slot[MAX_SOCKET];
    char buffer[MAX_INFO];
    uint8_t udpbuffer[MAX_UDP_PACKAGE];/*buffer used to recive udp msg*/
    fd_set rfds; /*fd  group : used for select of fds*/
};


struct request_open {
    int id;         /*id of the socket*/
    int port;       /*port of the machine*/
    uintptr_t opaque;/*id of module socket belong to*/
    char host[1];   /*host if the socket need to open*/
};

struct request_send {
    int id;
    int sz;         /*data size*/
    char *buffer;   /*payload ptr*/
};

struct request_send_udp {
    struct request_send send;   /*payload*/
    uint8_t address[UDP_ADDRESS_SIZE]; /*udp address*/
};

struct request_set_udp {
    int id;
    uint8_t address[UDP_ADDRESS_SIZE]; /*string address of the udp*/
};

struct request_close {
    int id;
    uintptr_t opaque; /*id of module socket belong to*/
};

struct request_listen {
    int id;
    int fd;         /*listen fd*/
    uintptr_t opaque;
    char host[1];
};

struct request_bind {
    int id;
    int fd;
    uintptr_t opaque;
};

struct request_start {
    int id;
    uintptr_t opaque;
};

struct request_set_opt {
    int id;
    int what;   /*key for the set_opt   */
    int value;  /*bal for set key = val */
};

struct request_udp {
    int id;
    int fd;
    int family; /*family of protocol*/
    uintptr_t opaque;
};

/*
    the  first byte is type
    S start socket
    
    B bind socket
 
    L listen socket
 
    K close socket
 
    O connect to (Open)
 
    X exit
 
    D send package (high)
    
    P send package (low)
 
    A send udp package 
 
    T set opt
 
    U create udp socket
 
    C set udp address
 */

struct request_package {
    uint8_t header[8]; //6 bytes dummy ahead 6 byte not used [0-5] , [6] is type ,[7] is len of package
    union {
        char buffer[256];
        struct request_open open;
        struct request_send send;
        struct request_send_udp send_udp;
        struct request_close close;
        struct request_listen listen;
        struct request_bind  bind;
        struct request_start start;
        struct request_set_opt set_opt;
        struct request_udp  udp;
        struct request_set_udp set_udp;
    }u;
    uint8_t dummy[256];
};
/**
 *  normal sockaddr storage
 */
union sockaddr_all {
    struct sockaddr s;          /*normal storage*/
    struct sockaddr_in v4;      /*ipv4 storage*/
    struct sockaddr_in6 v6;     /*ipv6 storage*/
};

/* warp of obj wait to send*/
struct send_object {
    void * buffer;              /*payload*/
    int sz;                     /*size of payload*/
    void (*free_func)(void *);  /*callback used to free payload*/
};




static inline void
clear_wb_list(struct wb_list *list) {
    list->head = NULL;
    list->tail = NULL;
}


static inline void
write_buffer_free(struct socket_server *ss,struct write_buffer *wb) {
    if(wb->userobject) {
        ss->soi.free(wb->buffer);
    } else {
        mtask_free(wb->buffer);
    }
    mtask_free(wb);
}

/*free all in write buffer list and init the write_buffer list*/
static void
free_wb_list(struct socket_server *ss, struct wb_list *list) {
    struct write_buffer *wb = list->head;
    while (wb) {
        struct write_buffer *tmp = wb;
        wb = wb->next;
        write_buffer_free(ss, tmp);
    }
    list->head = NULL;
    list->tail = NULL;
}

static void
send_request(struct socket_server *ss,struct request_package *request,char type, int len) {
    request->header[6] = (uint8_t)type;
    request->header[7] = (uint8_t)len;

    for (;;) {
    
        int n = write(ss->sendctrl_fd, &request->header[6], len+2);
        if (n<0) {
            if(errno != EINTR) {
                fprintf(stderr, "socket-server : send ctrl command error %s.\n", strerror(errno));
            }
            continue;
        }
        assert(n == len +2);
        return;
    }
}

static int
do_bind(const char *host,int port,int protocol,int *family) {
    int fd;
    int status;
    int reuse =1;
    
    struct addrinfo ai_hints;
    struct addrinfo *ai_list = NULL;
    char portstr[16];
    if (host == NULL || host[0] == 0) {
        host = "0.0.0."; //INADDR_ANY
    }
    sprintf(portstr, "%d",port);
    memset(&ai_hints, 0, sizeof(ai_hints));
    ai_hints.ai_family = AF_UNSPEC;
    if (protocol == IPPROTO_TCP) {
        ai_hints.ai_socktype = SOCK_STREAM;
    } else {
        assert(protocol == IPPROTO_UDP);
        ai_hints.ai_socktype = SOCK_DGRAM;
    }
    ai_hints.ai_protocol = protocol;
    
    status = getaddrinfo(host, portstr, &ai_hints, &ai_list);
    if (status != 0) {
        return -1;
    }
    
    *family = ai_list->ai_family;
    /*init socket*/
    fd = socket(*family, ai_list->ai_socktype, 0);
    if (fd<0) {
        goto _failed_fd;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void*)&reuse, sizeof(int)) == -1) {
        goto _failed;
    }
    
    /*bind address*/
    status = bind(fd, (struct sockaddr *)ai_list->ai_addr, ai_list->ai_addrlen);
    if (status != 0) {
        goto _failed;
    }
    freeaddrinfo(ai_list);
    return fd;
    
_failed_fd:
    close(fd);
_failed:
    freeaddrinfo(ai_list);
    return -1;
}

static int
do_listen(const char * host,int port,int backlog) {
    int family = 0;
    int listen_fd = do_bind(host,port,IPPROTO_TCP,&family);
    if (listen_fd<0) {
        return -1;
    }
    if (listen(listen_fd, backlog) == -1) {
        close(listen_fd);
        return -1;
    }
    return listen_fd;
}

static int
reserve_id(struct socket_server *ss) {
    int i;
    /*lookup all socket*/
    for (i=0; i<MAX_SOCKET; i++) {
        /*here is will alloc as 1 ,2, 3, INT_MAX, INT_MAX - 1 ... 3 2 1*/
        int id = __sync_add_and_fetch(&(ss->alloc_id), 1);
        if (id < 0) {
            id = __sync_and_and_fetch(&(ss->alloc_id), 0x7fffffff);
        }
        /*lookup slot by hash*/
        struct socket *s = &ss->slot[HASH_ID(id)];
        /*try to get the empty slots*/
        if (s->type == SOCKET_TYPE_INVALID) {
            if (__sync_bool_compare_and_swap(&s->type, SOCKET_TYPE_INVALID, SOCKET_TYPE_RESERVE)) {
                s->id = id;
                s->fd = -1;
                return id;
            } else {
                /*retry*/
                --i;
            }
        }
    }
    return -1;
}

/*build and send socket listen request to socket thread*/
int
socket_server_listen(struct socket_server *ss,uint32_t opaque,const char *addr,int port,int backlog) {
    int fd = do_listen(addr, port, backlog);
    if (fd<0) {
        return -1;
    }
    
    struct request_package request;
    int id = reserve_id(ss);
    
    if (id<0) {
        close(fd);
        return id;
    }
    
    request.u.listen.opaque = opaque;
    request.u.listen.id = id;
    request.u.listen.fd = fd;
    send_request(ss, &request, 'L', sizeof(request.u.listen));
    
    return id;
}

static int
open_request(struct socket_server *ss,struct request_package *req,uintptr_t opaque,const char *addr,int port) {
    int len = strlen(addr);
    if (len + sizeof(req->u.open) >= 256) {
        fprintf(stderr, "socket-server: Invalid addr %s.\n",addr);
        return -1;
    }
    /*alloc the socket id*/
    int id = reserve_id(ss);
    if (id<0) {
        return -1;
    }
    
    req->u.open.opaque = opaque;
    req->u.open.id = id;
    req->u.open.port = port;
    memcpy(req->u.open.host, addr, len);
    req->u.open.host[len] = '\0';
    
    return len;
}

static void
force_close(struct socket_server *ss,struct socket *s, struct socket_message *m) {
    m->id = s->id;
    m->ud = 0;
    m->data = NULL;
    m->opaque = s->opaque;
    if(s->type == SOCKET_TYPE_INVALID) return;
 
    assert(s->type != SOCKET_TYPE_RESERVE);
    free_wb_list(ss, &s->high);
    free_wb_list(ss, &s->low);
    
    if(s->type != SOCKET_TYPE_PACCEPT && s->type != SOCKET_TYPE_PLISTEN) {
        sp_del(ss->event_fd, s->fd);
    }
    
    if(s->type != SOCKET_TYPE_BIND) {
        close(s->fd);
    }
    s->type = SOCKET_TYPE_INVALID;
}

static int
has_cmd(struct socket_server *ss) {
    /*nonblock*/
    struct timeval tv = {0,0};
    int retval;
    FD_SET(ss->recvctrl_fd,&ss->rfds);
    
    retval = select(ss->recvctrl_fd+1, &ss->rfds, NULL, NULL, &tv);
    
    if (retval == 1)return 1;
    
    return 0;
}
/* block read ctrl msg from the pipe*/
static void
block_readpipe(int pipefd,void *buffer,int sz) {
    for (;;) {
        /*try to read msg from pipe*/
        int n = read(pipefd, buffer, sz);
        if (n<0) {
            /*continue if EINTR occured*/
            if (errno == EINTR)
                continue;
            fprintf(stderr, "socket-server : read pipe error %s.\n",strerror(errno));//打印出错信息
            return;
        }
        /*must atomic read from a pipe*/
        assert(n == sz);
        return;
    }
}
/*start socket after the socket attribute init*/
static int
start_socket(struct socket_server *ss,struct request_start *request,struct socket_message *result) {
    int id = request->id;
    /*build result msg*/
    result->id = id;
    result->opaque = request->opaque;
    result->ud = 0;
    result->data = NULL;
    
    struct socket *s = &ss->slot[HASH_ID(id)];
    
    /*check the socket wait to start*/
    if (s->type == SOCKET_TYPE_INVALID || s->id != id) {
        return SOCKET_ERROR;
    }
    
    /*start socket by register the read event if the type in (PACCEPT/PLISTEN)*/
    if (s->type == SOCKET_TYPE_PACCEPT || s->type == SOCKET_TYPE_PLISTEN) {
        /*register the read event*/
        if (sp_add(ss->event_fd, s->fd, s)) {
            s->type = SOCKET_TYPE_INVALID;
            return SOCKET_ERROR;
        }
        /*update the type of the socket*/
        s->type = (s->type == SOCKET_TYPE_PACCEPT) ? SOCKET_TYPE_CONNECTED : SOCKET_TYPE_LISTEN;
        s->opaque = request->opaque;
        result->data = "start";
        return SOCKET_OPEN;
    } else if (s->type == SOCKET_TYPE_CONNECTED) {
        /*update module id if the socket connected*/
        s->opaque = request->opaque;
        result->data = "transfer";
        return SOCKET_OPEN;
    }
    return -1;
}

static inline void
check_wb_list(struct wb_list *s) {
    assert(s->head == NULL);
    assert(s->tail == NULL);
}

static struct socket *
new_fd(struct socket_server *ss, int id, int fd, int protocol, uintptr_t opaque, bool add) {
    struct socket * s = &ss->slot[HASH_ID(id)];
    assert(s->type == SOCKET_TYPE_RESERVE);
    
    if (add) {
        if (sp_add(ss->event_fd, fd, s)) {
            s->type = SOCKET_TYPE_INVALID;
            return NULL;
        }
    }
    
    s->id = id;
    s->fd = fd;
    s->protocol = protocol;
    s->p.size = MIN_READ_BUFFER;
    s->opaque = opaque;
    s->wb_size = 0;
    check_wb_list(&s->high);
    check_wb_list(&s->low);
    return s;
}


/*deal with the ctrl msg*/
static int
ctrl_cmd(struct socket_server *ss,struct socket_message *result) {
    int fd = ss->recvctrl_fd;
    /*the length of message is one byte ,so 256+8 buffer size is enough*/
    uint8_t buffer[256];
    uint8_t header[2];
    
    block_readpipe(fd, header, sizeof(header));
    
    int type = header[0]; //msg type
    int len = header[1];  //msg sz
    
    block_readpipe(fd, buffer, len);
    /*ctrl command only exist in local fd,so don't worry about endian*/
    switch (type) {
        case 'S':
            /*strat socket*/
            return start_socket(ss, (struct request_start *)buffer, result);
        
            
        default:
            break;
    }
    
    return -1;
}
/*convert object to send_object;  sz sz < 0 ? object mem managered by usr api:object mem managered by mtask api*/

static inline bool
send_object_init(struct socket_server *ss, struct send_object *so,void *object,int sz) {
    if (sz < 0) {
        so->buffer = ss->soi.buffer(object);
        so->sz = ss->soi.size(object);
        so->free_func = ss->soi.free;
        return true;
    } else {
        so->buffer = object;
        so->sz = sz;
        so->free_func = mtask_free;
        return false;
    }
}

static void
free_buffer(struct socket_server *ss,const void *buffer,int sz) {
    struct send_object so;
    send_object_init(ss, &so,(void *)buffer,sz);
    so.free_func((void*)buffer);
}

struct socket_server *
socket_server_create() {
    int i ;
    int fd[2];
    
    poll_fd efd = sp_create();
    if(sp_invalid(efd)) {
        fprintf(stderr, "socket-server: create event pool faild.\n");
        return NULL;
    }
    //create
    if(pipe(fd)) {
        sp_release(efd);
        fprintf(stderr, "socket-server: create socket pair failed.\n");
        return NULL;
    }
    //listen
    if(sp_add(efd, fd[0], NULL)) {
        fprintf(stderr, "socket-server: can't add server fd to event pool.\n");
        close(fd[0]);
        close(fd[1]);
        sp_release(efd);
        return NULL;
    }
    struct socket_server *ss = mtask_malloc(sizeof(*ss));
    ss->event_fd = efd;
    ss->recvctrl_fd = fd[0];
    ss->sendctrl_fd = fd[1];
    ss->checkctrl = 1;
    
    /*init all socket handle*/
    for (i=0; i<MAX_SOCKET; i++) {
        struct socket * s = &ss->slot[i];
        s->type = SOCKET_TYPE_INVALID;
        clear_wb_list(&s->high);
        clear_wb_list(&s->low);
    }
    ss->alloc_id = 0;
    ss->event_n = 0;
    ss->event_index = 0;
    memset(&ss->soi, 0, sizeof(ss->soi));
    FD_ZERO(&ss->rfds);
    assert(ss->recvctrl_fd < FD_SETSIZE);
    
    return ss;
}

void
socket_server_exit(struct socket_server *ss) {
    struct request_package request;
    send_request(ss, &request, 'X', 0);
}

void
socket_server_start(struct socket_server *ss,uintptr_t opaque,int id) {
    struct request_package  request;
    request.u.start.id = id;
    request.u.start.opaque = opaque;
    send_request(ss, &request, 'S', sizeof(request.u.start));
}

static int
send_list_tcp(struct socket_server *ss, struct socket *s, struct wb_list *list, struct socket_message *result) {
    while (list->head) {
        struct write_buffer * tmp = list->head;
        for (;;) {
            int sz = write(s->fd, tmp->ptr, tmp->sz);
            if (sz < 0) {
                switch(errno) {
                    case EINTR:
                        continue;
                    case EAGAIN:
                        return -1;
                }
                force_close(ss,s, result);
                return SOCKET_CLOSE;
            }
            s->wb_size -= sz;
            if (sz != tmp->sz) {
                tmp->ptr += sz;
                tmp->sz -= sz;
                return -1;
            }
            break;
        }
        list->head = tmp->next;
        write_buffer_free(ss,tmp);
    }
    list->tail = NULL;
    
    return -1;
}



static socklen_t
udp_socket_address(struct socket *s, const uint8_t udp_address[UDP_ADDRESS_SIZE], union sockaddr_all *sa) {
    int type = (uint8_t)udp_address[0];
    if (type != s->protocol)
        return 0;
    uint16_t port = 0;
    memcpy(&port, udp_address+1, sizeof(uint16_t));
    switch (s->protocol) {
        case PROTOCOL_UDP:
            memset(&sa->v4, 0, sizeof(sa->v4));
            sa->s.sa_family = AF_INET;
            sa->v4.sin_port = port;
            memcpy(&sa->v4.sin_addr, udp_address + 1 + sizeof(uint16_t), sizeof(sa->v4.sin_addr));	// ipv4 address is 32 bits
            return sizeof(sa->v4);
        case PROTOCOL_UDPv6:
            memset(&sa->v6, 0, sizeof(sa->v6));
            sa->s.sa_family = AF_INET6;
            sa->v6.sin6_port = port;
            memcpy(&sa->v6.sin6_addr, udp_address + 1 + sizeof(uint16_t), sizeof(sa->v6.sin6_addr));	// ipv4 address is 128 bits
            return sizeof(sa->v6);
    }
    return 0;
}



static int
send_list_udp(struct socket_server *ss, struct socket *s, struct wb_list *list, struct socket_message *result) {
    while (list->head) {
        struct write_buffer * tmp = list->head;
        union sockaddr_all sa;
        socklen_t sasz = udp_socket_address(s, tmp->udp_address, &sa);
        int err = sendto(s->fd, tmp->ptr, tmp->sz, 0, &sa.s, sasz);
        if (err < 0) {
            switch(errno) {
                case EINTR:
                case EAGAIN:
                    return -1;
            }
            fprintf(stderr, "socket-server : udp (%d) sendto error %s.\n",s->id, strerror(errno));
            return -1;
        }
        
        s->wb_size -= tmp->sz;
        list->head = tmp->next;
        write_buffer_free(ss,tmp);
    }
    list->tail = NULL;
    
    return -1;
}


static int
send_list(struct socket_server *ss, struct socket *s, struct wb_list *list, struct socket_message *result) {
    if (s->protocol == PROTOCOL_TCP) {
        return send_list_tcp(ss, s, list, result);
    } else {
        return send_list_udp(ss, s, list, result);
    }
}


void
socket_server_close(struct socket_server *ss,uintptr_t opaque,int id) {
    struct request_package  request;
    request.u.close.id = id;
    request.u.close.opaque = opaque;
    send_request(ss, &request, 'K', sizeof(request.u.close));
}

static inline int
list_uncomplete(struct wb_list *s) {
    struct write_buffer *wb = s->head;
    if (wb == NULL)
        return 0;
    
    return (void *)wb->ptr != wb->buffer;
}

static void
raise_uncomplete(struct socket * s) {
    struct wb_list *low = &s->low;
    struct write_buffer *tmp = low->head;
    low->head = tmp->next;
    if (low->head == NULL) {
        low->tail = NULL;
    }
    
    // move head of low list (tmp) to the empty high list
    struct wb_list *high = &s->high;
    assert(high->head == NULL);
    
    tmp->next = NULL;
    high->head = high->tail = tmp;
}


// return -1 (ignore) when error
static int
forward_message_tcp(struct socket_server *ss, struct socket *s, struct socket_message * result) {
    int sz = s->p.size;
    char * buffer = mtask_malloc(sz);
    int n = (int)read(s->fd, buffer, sz);
    if (n<0) {
        mtask_free(buffer);
        switch(errno) {
            case EINTR:
                break;
            case EAGAIN:
                fprintf(stderr, "socket-server: EAGAIN capture.\n");
                break;
            default:
                // close when error
                force_close(ss, s, result);
                return SOCKET_ERROR;
        }
        return -1;
    }
    if (n==0) {
        mtask_free(buffer);
        force_close(ss, s, result);
        return SOCKET_CLOSE;
    }
    
    if (s->type == SOCKET_TYPE_HALFCLOSE) {
        // discard recv data
        mtask_free(buffer);
        return -1;
    }
    
    if (n == sz) {
        s->p.size *= 2;
    } else if (sz > MIN_READ_BUFFER && n*2 < sz) {
        s->p.size /= 2;
    }
    
    result->opaque = s->opaque;
    result->id = s->id;
    result->ud = n;
    result->data = buffer;
    return SOCKET_DATA;
}

static void
socket_keepalive(int fd) {
    int keepalive = 1;
    setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&keepalive , sizeof(keepalive));
}


// return 0 when failed
static int
report_accept(struct socket_server *ss, struct socket *s, struct socket_message *result) {
    union sockaddr_all u;
    socklen_t len = sizeof(u);
    int client_fd = accept(s->fd, &u.s, &len);
    if (client_fd < 0) {
        return 0;
    }
    int id = reserve_id(ss);
    if (id < 0) {
        close(client_fd);
        return 0;
    }
    socket_keepalive(client_fd);
    sp_nonblocking(client_fd);
    struct socket *ns = new_fd(ss, id, client_fd, PROTOCOL_TCP, s->opaque, false);
    if (ns == NULL) {
        close(client_fd);
        return 0;
    }
    ns->type = SOCKET_TYPE_PACCEPT;
    result->opaque = s->opaque;
    result->id = s->id;
    result->ud = id;
    result->data = NULL;
    
    void * sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void *)&u.v6.sin6_addr;
    int sin_port = ntohs((u.s.sa_family == AF_INET) ? u.v4.sin_port : u.v6.sin6_port);
    char tmp[INET6_ADDRSTRLEN];
    if (inet_ntop(u.s.sa_family, sin_addr, tmp, sizeof(tmp))) {
        snprintf(ss->buffer, sizeof(ss->buffer), "%s:%d", tmp, sin_port);
        result->data = ss->buffer;
    }
    
    return 1;
}

static int
gen_udp_address(int protocol,union sockaddr_all *sa,uint8_t *udp_address) {
    int addrsz = 1;
    udp_address[0] = (uint8_t)protocol;
    /*convert by the protocol*/
    if (protocol == PROTOCOL_UDP) {
        memcpy(udp_address+addrsz, &sa->v4.sin_port, sizeof(sa->v4.sin_port));
        addrsz += sizeof(sa->v4.sin_port);
        memcpy(udp_address+addrsz, &sa->v4.sin_addr, sizeof(sa->v4.sin_addr));
        addrsz += sizeof(sa->v4.sin_addr);
    } else {
        memcpy(udp_address+addrsz, &sa->v6.sin6_port, sizeof(sa->v6.sin6_port));
        addrsz += sizeof(sa->v6.sin6_port);
        memcpy(udp_address+addrsz, &sa->v6.sin6_addr, sizeof(sa->v6.sin6_addr));
        addrsz += sizeof(sa->v6.sin6_addr);
    }
    return addrsz;
}

static inline void
clear_closed_event(struct socket_server *ss, struct socket_message * result, int type) {
    if (type == SOCKET_CLOSE || type == SOCKET_ERROR) {
        int id = result->id;
        int i;
        for (i=ss->event_index; i<ss->event_n; i++) {
            struct event *e = &ss->ev[i];
            struct socket *s = e->s;
            if (s) {
                if (s->type == SOCKET_TYPE_INVALID && s->id == id) {
                    e->s = NULL;
                }
            }
        }
    }
}

static inline int
send_buffer_empty(struct socket *s) {
    return (s->high.head == NULL && s->low.head == NULL);
}



static int
report_connect(struct socket_server *ss, struct socket *s, struct socket_message *result) {
    int error;
    socklen_t len = sizeof(error);
    int code = getsockopt(s->fd, SOL_SOCKET, SO_ERROR, &error, &len);
    if (code < 0 || error) {
        force_close(ss,s, result);
        return SOCKET_ERROR;
    } else {
        s->type = SOCKET_TYPE_CONNECTED;
        result->opaque = s->opaque;
        result->id = s->id;
        result->ud = 0;
        if (send_buffer_empty(s)) {
            sp_write(ss->event_fd, s->fd, s, false);
        }
        union sockaddr_all u;
        socklen_t slen = sizeof(u);
        if (getpeername(s->fd, &u.s, &slen) == 0) {
            void * sin_addr = (u.s.sa_family == AF_INET) ? (void*)&u.v4.sin_addr : (void *)&u.v6.sin6_addr;
            if (inet_ntop(u.s.sa_family, sin_addr, ss->buffer, sizeof(ss->buffer))) {
                result->data = ss->buffer;
                return SOCKET_OPEN;
            }
        }
        result->data = NULL;
        return SOCKET_OPEN;
    }
}



/*
	Each socket has two write buffer list, high priority and low priority.
 
	1. send high list as far as possible.
	2. If high list is empty, try to send low list.
	3. If low list head is uncomplete (send a part before), move the head of low list to empty high list (call raise_uncomplete) .
	4. If two lists are both empty, turn off the event. (call check_close)
 */
static int
send_buffer(struct socket_server *ss, struct socket *s, struct socket_message *result) {
    assert(!list_uncomplete(&s->low));
    // step 1
    if (send_list(ss,s,&s->high,result) == SOCKET_CLOSE) {
        return SOCKET_CLOSE;
    }
    if (s->high.head == NULL) {
        // step 2
        if (s->low.head != NULL) {
            if (send_list(ss,s,&s->low,result) == SOCKET_CLOSE) {
                return SOCKET_CLOSE;
            }
            // step 3
            if (list_uncomplete(&s->low)) {
                raise_uncomplete(s);
            }
        } else {
            // step 4
            sp_write(ss->event_fd, s->fd, s, false);
            
            if (s->type == SOCKET_TYPE_HALFCLOSE) {
                force_close(ss, s, result);
                return SOCKET_CLOSE;
            }
        }
    }
    
    return -1;
}

static int
forward_message_udp(struct socket_server *ss, struct socket *s, struct socket_message * result) {
    union sockaddr_all sa;
    socklen_t slen = sizeof(sa);
    int n = recvfrom(s->fd, ss->udpbuffer,MAX_UDP_PACKAGE,0,&sa.s,&slen);
    if (n<0) {
        switch(errno) {
            case EINTR:
            case EAGAIN:
                break;
            default:
                // close when error
                force_close(ss, s, result);
                return SOCKET_ERROR;
        }
        return -1;
    }
    uint8_t * data;
    if (slen == sizeof(sa.v4)) {
        if (s->protocol != PROTOCOL_UDP)
            return -1;
        data = mtask_malloc(n + 1 + 2 + 4);
        gen_udp_address(PROTOCOL_UDP, &sa, data + n);
    } else {
        if (s->protocol != PROTOCOL_UDPv6)
            return -1;
        data = mtask_malloc(n + 1 + 2 + 16);
        gen_udp_address(PROTOCOL_UDPv6, &sa, data + n);
    }
    memcpy(data, ss->udpbuffer, n);
    
    result->opaque = s->opaque;
    result->id = s->id;
    result->ud = n;
    result->data = (char *)data;
    
    return SOCKET_UDP;
}



/*wait event by epoll*/
int
socket_server_poll(struct socket_server *ss, struct socket_message *result, int *more) {
    for (;;) {
        /*1: try to get the ctrl msg from modules by pipe*/
        if (ss->checkctrl) {
            /*try to get msg from pipe*/
            if (has_cmd) {
                /*run ctrl msg and get the result*/
                int type = ctrl_cmd(ss, result);
                if (type != -1) {
                    /*fail ,so we clear the event*/
                    clear_closed_event(ss, result, type);
                    return type;
                } else continue;
            } else {
                ss->checkctrl = 0; //flag == false
            }
        }
        if (ss->event_index == ss->event_n) {
            ss->event_n = sp_wait(ss->event_fd, ss->ev, MAX_EVENT);
            ss->checkctrl = 1;
            if (more) {
                *more = 0;
            }
            ss->event_index = 0;
            if (ss->event_n <= 0) {
                ss->event_n = 0;
                return -1;
            }
        }
        struct event *e = &ss->ev[ss->event_index++];
        struct socket *s = e->s;
        if (s == NULL) {
            // dispatch pipe message at beginning
            continue;
        }
        switch (s->type) {
            case SOCKET_TYPE_CONNECTING:
                return report_connect(ss, s, result);
            case SOCKET_TYPE_LISTEN:
                if (report_accept(ss, s, result)) {
                    return SOCKET_ACCEPT;
                }
                break;
            case SOCKET_TYPE_INVALID:
                fprintf(stderr, "socket-server: invalid socket\n");
                break;
            default:
                if (e->read) {
                    int type;
                    if (s->protocol == PROTOCOL_TCP) {
                        type = forward_message_tcp(ss, s, result);
                    } else {
                        type = forward_message_udp(ss, s, result);
                        if (type == SOCKET_UDP) {
                            // try read again
                            --ss->event_index;
                            return SOCKET_UDP;
                        }
                    }
                    if (e->write) {
                        // Try to dispatch write message next step if write flag set.
                        e->read = false;
                        --ss->event_index;
                    }
                    if (type == -1)
                        break;
                    clear_closed_event(ss, result, type);
                    return type;
                }
                if (e->write) {
                    int type = send_buffer(ss, s, result);
                    if (type == -1)
                        break;
                    clear_closed_event(ss, result, type);
                    return type;
                }
                break;
        }

    }
}

/*release all things in socket manager*/
void
socket_server_release(struct socket_server *ss) {
    int i;
    struct socket_message dummy;
    /*release the socket*/
    for (i=0; i<MAX_SOCKET; i++) {
        struct socket *s =&ss->slot[i];
        if(s->type != SOCKET_TYPE_RESERVE) {
            force_close(ss, s, &dummy);
        }
    }
    /*close ctrl pipe*/
    close(ss->sendctrl_fd);
    close(ss->recvctrl_fd);
    /*close epoll*/
    sp_release(ss->event_fd);
    mtask_free(ss);
}
/*build and send payload high;  return -1 when error*/
int64_t
socket_server_send(struct socket_server *ss, int id,const void *buffer,int sz) {
    struct socket *s = &ss->slot[HASH_ID(id)];
    if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
        free_buffer(ss, buffer, sz);
        return -1;
    }
    struct request_package request;
    request.u.send.id = id;
    request.u.send.sz = sz;
    request.u.send.buffer = (char*)buffer;
    
    send_request(ss, &request, 'D', sizeof(request.u.send));
    return s->wb_size;
}


int
socket_server_connect(struct socket_server *ss,uintptr_t opaque,const char * addr, int port) {
    struct request_package request;
    /*try to build open request*/
    int len = open_request(ss, &request, opaque, addr, port);
    if (len<0) {
        return -1;
    }
    /*send the request to the socket manager*/
    send_request(ss, &request, '0', sizeof(request.u.open)+len);
    
    return request.u.open.id;
}
/*build and send payload low*/
void
socket_server_send_lowpriority(struct socket_server *ss, int id, const void * buffer, int sz) {
    struct socket *s = &ss->slot[HASH_ID(id)];
    if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
        free_buffer(ss, buffer, sz);
        return;
    }
    
    struct request_package request;
    request.u.send.id = id;
    request.u.send.sz = sz;
    request.u.send.buffer = (char*)buffer;
    
    send_request(ss, &request, 'P', sizeof(request.u.send));
}
/*build and send bind request to socket thread*/
int
socket_server_bind(struct socket_server *ss ,uintptr_t opaque, int fd) {
    struct request_package request;
    int id = reserve_id(ss);
    if (id<0) {
        return -1;
    }
    request.u.bind.opaque = opaque;
    request.u.bind.id  = id;
    request.u.bind.fd = fd;
    send_request(ss, &request, 'B', sizeof(request.u.bind));
    return id;
}
/*build and send nodelay request to socket thread*/
void
socket_server_nodelay(struct socket_server *ss,int id) {
    struct request_package request;
    request.u.set_opt.id = id;
    request.u.set_opt.what = TCP_NODELAY;
    request.u.set_opt.value = 1;
    send_request(ss, &request, 'T', sizeof(request.u.set_opt));
}


// create an udp socket handle, attach opaque with it . udp socket don't need call socket_server_start to recv message
// if port != 0, bind the socket . if addr == NULL, bind ipv4 0.0.0.0 . If you want to use ipv6, addr can be "::" and port 0.
int
socket_server_udp(struct socket_server *ss, uintptr_t opaque, const char * addr, int port) {
    int fd;
    int family;
    /*work as a servr*/
    if (port != 0 || addr != NULL) {
        fd = do_bind(addr, port, IPPROTO_UDP, &family);
        if (fd<0) {
            return -1;
        }
    } else {
        /*work as a client*/
        family = AF_INET;
        fd  = socket(family,SOCK_DGRAM,0);
        if (fd<0) {
            return -1;
        }
    }
    /*set nonblock*/
    sp_nonblocking(fd);
    
    int id = reserve_id(ss);
    if (id < 0) {
        close(fd);
        return -1;
    }
    /**build request msg*/
    struct request_package request;
    request.u.udp.id = id;
    request.u.udp.fd = fd;
    request.u.udp.opaque = opaque;
    request.u.udp.family = family;
    
    send_request(ss, &request, 'U', sizeof(request.u.udp));
    
    return id;
}




/* set default dest address, return 0 when success*/
int
socket_server_udp_connect(struct socket_server *ss, int id, const char * addr, int port) {
    int status;
    struct addrinfo ai_hints;
    struct addrinfo *ai_list = NULL;
    char portstr[16];
    sprintf(portstr, "%d",port);
    
    /*get the udp address*/
    memset(&ai_hints, 0, sizeof(ai_hints));
    ai_hints.ai_family = AF_UNSPEC;
    ai_hints.ai_socktype = SOCK_DGRAM;
    ai_hints.ai_protocol = IPPROTO_UDP;
    
    status = getaddrinfo(addr, portstr, &ai_hints, &ai_list);
    if (status !=0) {
        return -1;
    }
    struct request_package request;
    request.u.set_udp.id = id;
    
    int protocol;
    
    if (ai_list->ai_family == AF_INET) {
        protocol = PROTOCOL_UDP;
    } else if(ai_list->ai_family == AF_INET6) {
        protocol = PROTOCOL_UDPv6;
    } else {
        freeaddrinfo(ai_list);
        return -1;
    }
    /*get the public udp address*/
    int addrsz = gen_udp_address(protocol, (union sockaddr_all*)ai_list->ai_addr, request.u.set_udp.address);
    
    freeaddrinfo(ai_list);
    
    send_request(ss, &request, 'C', sizeof(request.u.set_udp)+addrsz);
    return 0;
}

int64_t
socket_server_udp_send(struct socket_server *ss, int id, const struct socket_udp_address *addr, const void *buffer, int sz) {
    struct socket *s = &ss->slot[HASH_ID(id)];
    if (s->id != id || s->type == SOCKET_TYPE_INVALID) {
        free_buffer(ss, buffer, sz);
        return -1;
    }
    
    struct request_package request;
    request.u.send_udp.send.id = id;
    request.u.send_udp.send.sz = sz;
    request.u.send_udp.send.buffer = (char *)buffer;
    
    const uint8_t *udp_address = (const uint8_t *)addr;
    int addrsz;
    switch (udp_address[0]) {
        case PROTOCOL_UDP:
            addrsz = 1+2+4; /*1type,2port,4ipv4*/
            break;
         case PROTOCOL_UDPv6:
            addrsz =1+2+16;/*1type,2port,16ipv6*/
            break;
        default:
            free_buffer(ss, buffer, sz);
            return -1;
    }
    memcpy(request.u.set_udp.address, udp_address, addrsz);
    /*trans udp data to socket thread here(size=payload +udp address size)*/
    send_request(ss, &request, 'A', sizeof(request.u.send_udp.send)+addrsz);
    
    return s->wb_size;
}

const struct socket_udp_address *
socket_server_udp_address(struct socket_server *ss ,struct socket_message *msg,int *addrsz) {
    /*address is in the end of the msg data*/
    uint8_t * address = (uint8_t *)(msg->data + msg->ud);
    int type = address[0];
    switch (type) {
        case PROTOCOL_UDP:
            *addrsz = 1+2+4;
            break;
        case PROTOCOL_UDPv6:
            *addrsz = 1+2+16;
            break;
            
        default:
            return NULL;
    }
    return (const struct socket_udp_address*)address;
}









