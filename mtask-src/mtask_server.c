//
//  mtask_server.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include "mtask.h"
#include "mtask_server.h"
#include "mtask_module.h"
#include "mtask_mq.h"
#include "mtask_handle.h"
#include "mtask_harbor.h"
#include "mtask_monitor.h"
#include "mtask_timer.h"
#include "mtask_imp.h"

#include <pthread.h>

#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>


#ifdef CALLING_CHECK

//type __sync_lock_test_and_set (type *ptr, type value, ...)
//将*ptr设为value并返回*ptr操作之前的值

//void __sync_lock_release (type *ptr, ...)
//将*ptr置0

#define CHECKCALLING_BEGIN(ctx) assert(__sync_lock_test_and_set(&ctx->calling,1) == 0);	//检查调用开始
#define CHECKCALLING_END(ctx) __sync_lock_release(&ctx->calling);//检查调用结束
#define CHECKCALLING_INIT(ctx) ctx->calling = 0;                 //检查调用初始化
#define CHECKCALLING_DECL int calling;                           //检查调用声明

#else

#define CHECKCALLING_BEGIN(ctx)
#define CHECKCALLING_END(ctx)
#define CHECKCALLING_INIT(ctx)
#define CHECKCALLING_DECL

#endif

struct mtask_context {
    void *instance;
    struct mtask_module *mod;
    void *cb_ud;
    mtask_cb cb;
    
    struct message_queue *queue;
    FILE *logfile;
    char result[32];
    uint32_t handle;
    
    int session_id;
    int ref;
    bool init;
    bool endless;
    
    CHECKCALLING_DECL
};

struct mtask_node {
    int total;
    int init;
    uint32_t monitor_exit;
    pthread_key_t handle_key; //pthread_getpecific和pthread_setspecific实现同一个线程中不同函数间共享数据的一种很好的方式.

};

struct command_func {
    const char *name;
    const char *(*func)(struct mtask_context *ctx,const char *parm);
};


static struct mtask_node G_NODE; /*global_node*/


struct drop_t {  /*need to drop handle */
    uint32_t handle;
};


static void
context_inc() {
    __sync_fetch_and_add(&G_NODE.total,1);
}

static void
context_dec() {
    __sync_fetch_and_sub(&G_NODE.total,1);
}

static void
delete_context(struct mtask_context *ctx) {
    if(ctx->logfile) {
        fclose(ctx->logfile);
    }
    mtask_module_instance_release(ctx->mod, ctx->instance);
    mtask_mq_mark_release(ctx->queue);
    mtask_free(ctx);
    context_dec();
}

/*drop msg from message_queue*/
static void
drop_message(struct mtask_message *msg,void *ud) {
    struct drop_t *d = ud;
    mtask_free(msg->data);
    uint32_t source = d->handle;
    assert(source);
    
    mtask_send(NULL, source, msg->source, PTYPE_ERROR, 0, NULL, 0);
}

static void
dispatch_message(struct mtask_context *ctx,struct mtask_message *msg) {
    assert(ctx->init);
    CHECKCALLING_BEGIN(ctx)
    
    pthread_setspecific(G_NODE.handle_key, (void*)(uintptr_t)(ctx->handle));//设置线程私有数据
    
    int type = msg->sz >> HANDLE_REMOTE_SHIFT;       //获取消息类别
    size_t sz = msg->sz & HANDLE_MASK;
    if(ctx->logfile) {
        
    }
    if(!ctx->cb(ctx,ctx->cb_ud,type,msg->session,msg->source,msg->data,sz)){
        mtask_free(msg->data);
    }
    
    CHECKCALLING_END(ctx)
}


struct mtask_context *
mtask_context_new(const char *name,const char *parm) {
    struct mtask_module *mod  = mtask_module_query(name);
    
    if(mod == NULL) return NULL;
    
    void *inst = mtask_module_instance_create(mod);
    if(inst == NULL) return NULL;
    
    struct mtask_context *ctx = mtask_malloc(sizeof(*ctx));
    CHECKCALLING_INIT(ctx);
    
    /*init */
    ctx->mod = mod;
    ctx->instance = inst;
    ctx->ref = 2;
    ctx->cb = NULL;
    ctx->cb_ud = NULL;
    ctx->session_id = 0;
    ctx->logfile = NULL;
    
    ctx->init = false;
    ctx->endless = false;
    // Should set to 0 first to avoid mtask_handle_retireall get an uninitialized handle
    ctx->handle = 0;
    
    ctx->handle = mtask_handle_register(ctx);
    struct message_queue *queue = ctx->queue = mtask_mq_create(ctx->handle);
    
    context_inc();
    
    CHECKCALLING_BEGIN(ctx);
    int r = mtask_module_instance_init(mod, inst, ctx, parm);
    if(r == 0) {
        struct mtask_context *ret = mtask_context_release(ctx);
        if(ret) {
            ctx->init = true;
        }
        mtask_globalmq_push(queue);
        if(ret) {
            mtask_error(ret, "LAUNCH %s %s", name, parm ? parm : "");//打印日志[:01000001] LAUNCH logger
        }
        return ret;
    } else {
        mtask_error(ctx, "FAILED launch %s",name);
        uint32_t handle = ctx->handle;
        mtask_context_release(ctx);
        mtask_handle_retire(handle);
        
        struct drop_t d  = {handle};
        mtask_mq_release(queue, drop_message, &d);             //释放队列
        return NULL;
        
    }
}

/**
 *  get the handle of mtask_context
 *
 *  @param ctx context
 *
 *  @return ctx->handle
 */
uint32_t
mtask_context_handle(struct mtask_context * ctx) {
    return ctx->handle;
}

struct mtask_context *
mtask_context_release(struct mtask_context *ctx) {
    if(__sync_sub_and_fetch(&ctx->ref,1) == 0) {
        delete_context(ctx);
        return NULL;
    }
    return ctx;
}


int
mtask_context_newsession(struct mtask_context *ctx) {
    int session = ++ctx->session_id;
    if (session <= 0) {
        ctx->session_id = 1;
        return 1;
    }
    return session;
}
/**
 *  分发消息(消息分发是启动多个worker thread不断地循环调用该函数完成的)
 *  waring这个函数有两种情况：
 *      1:传入的message_queue 为NULL
 *      2:非NULL
 *  @param sm     监察者对象
 *  @param q      服务队列
 *  @param weight 线程权重
 *
 *  @return message_queue
 *
 *   1:通过message_queue获得服务的handle
 *   2:通过handle查找到服务的context
 *   3:从message_queue中pop一个元素
 *   4:调用 dispatch_message进行消息分发
 *   5:如果全局队列为空,则直接返回此队列(这样下次就会继续处理这个队列,此函数是循环调用的)
 *   6:如果全局队列非空,则pop全局队列,得到下一个服务队列
 *   7:将此队列插入全局队列,返回下一个服务队列
 *
 *  只所以不一次性处理玩当前队列,而要用5～7的步骤,是为了消息调度的公平性,对每一个服务都公平.
 */
/* dispatch message : start N worker threads call this function (while) */
struct message_queue *
mtask_context_message_dispatch(struct mtask_monitor *m,struct message_queue *q,int weight) {
    if (q==NULL) {
        q = mtask_globalmq_pop();
        if (q==NULL) return NULL;
    }
    /*else mq != NULL */
    uint32_t handle = mtask_mq_handle(q);
    
    struct mtask_context *ctx = mtask_handle_grab(handle);
    if (ctx == NULL) {
        struct drop_t d = {handle};
        mtask_mq_release(q, drop_message, &d);
        return mtask_globalmq_pop();
    }
    
    int i ,n=1;
    struct mtask_message msg;
    /*dispatch msg of curr message_queue*/
    for (i=0; i<n; i++) {
        if (mtask_mq_pop(q, &msg)) { 
            mtask_context_release(ctx);
            return mtask_globalmq_pop();
        } else if (i == 0 && weight >= 0) {
            n = mtask_mq_length(q);
            n >>= weight;//权重值比较大时,对长度进行右移操作,也就是减少处理的消息数目
        }
        int overload = mtask_mq_overload(q);
        if (overload) {
            mtask_error(ctx, "May overload,message queue length =%d",overload);
        }
        
        mtask_monitor_trigger(m, msg.source, handle);
        
        if (ctx->cb == NULL) {
            mtask_free(msg.data);
        } else {
            dispatch_message(ctx, &msg);
        }
        mtask_monitor_trigger(m, 0, 0);
    }
    assert( q = ctx->queue);
    struct message_queue *mq = mtask_globalmq_pop();
    if (mq) {
        mtask_globalmq_push(q);
        q = mq;
    }
    mtask_context_release(ctx);
    return q;
}

void
mtask_context_dispatchall(struct mtask_context *ctx) {
    struct mtask_message msg;
    struct message_queue *q = ctx->queue;
    while (!mtask_mq_pop(q, &msg)) {                            //一直从队列中pop消息,直到没有消息
        dispatch_message(ctx, &msg);                            //分发消息
    }
}

int
mtask_context_push(uint32_t handle,struct mtask_message *message) {
    struct mtask_context * ctx = mtask_handle_grab(handle);    //根据服务获取context引用
    if (ctx == NULL) {
        return -1;
    }
    mtask_mq_push(ctx->queue, message);                         //将消息放入队列
    mtask_context_release(ctx);                                 //释放context,因为在mtask_handle_grab中调用了mtask_context_grab增加了context的引用计数
    
    return 0;
}

void
mtask_context_grab(struct mtask_context *ctx) {
    __sync_add_and_fetch(&ctx->ref,1);
}

void
mtask_context_endless(uint32_t handle) {
    struct mtask_context *ctx = mtask_handle_grab(handle);
    if(ctx == NULL) return;
    ctx->endless = true;
    mtask_context_release(ctx);
}

int
mtask_context_total() {
    return G_NODE.total;
}

void
mtask_global_init(void) {
    G_NODE.total = 0;
    G_NODE.monitor_exit = 0;
    G_NODE.init = 1;
    
    if (pthread_key_create(&G_NODE.handle_key, NULL)) {
        fprintf(stderr, "pthread_key_create failed");
        exit(1);
    }
    // set mainthread's key
    mtask_initthread(THREAD_MAIN);
}

void
mtask_global_exit(void) {
   //clear mainthread's data
    pthread_key_delete(G_NODE.handle_key);
}

void
mtask_initthread(int m) {
    //为了和service address显出不同来,这个就是做 log 用的
    uintptr_t v = (uint32_t)(-m);
    pthread_setspecific(G_NODE.handle_key, (void*)v);
}

static const char *
cmd_timeout(struct mtask_context *ctx,const char *parm) {
    char *session_ptr = NULL;
    int ti = strtol(parm, &session_ptr, 10);
    int session  = mtask_context_newsession(ctx);
    
    mtask_timeout(ctx->handle, ti, session);
    sprintf(ctx->result, "%d",session);
    return ctx->result;
}


static struct command_func cmd_funcs[] = {
    {"TIMEOUT",cmd_timeout},
    {NULL,NULL},
};


const char *
mtask_command(struct mtask_context *ctx,const char *cmd, const char *parm) {
    struct command_func *method = &cmd_funcs[0];
    while (method->name) {
        if (strcmp(cmd, method->name) == 0) {
            return method->func(ctx,parm);
        }
        ++method;
    }
    return NULL;
}


static void
_filter_args(struct mtask_context *ctx,int type,int *session,void **data, size_t *sz) {
    int needcopy = !(type & PTYPE_TAG_DONT_COPY);
    int allocsession = type & PTYPE_TAG_ALLOC_SESSION;
    type &= 0xff;
    
    if (allocsession) {
        assert(*session == 0);
        *session = mtask_context_newsession(ctx);
    }
    
    if (needcopy && *data) {
        char *msg = mtask_malloc(*sz+1);
        memcpy(msg, *data, *sz);
        msg[*sz] = '\0';
        *data = msg;
    }
    
    *sz |= type << HANDLE_REMOTE_SHIFT;
}

static void
copy_name(char name[GLOBALNAME_LENGTH],const char *addr) {
    int i;
    for (i=0; GLOBALNAME_LENGTH && addr[i]; i++) {
        name[i] = addr[i];
    }
    
    for (; i<GLOBALNAME_LENGTH; i++) {
        name[i] = '\0';
    }
}

int
mtask_send_name(struct mtask_context *ctx,uint32_t source,const char *address,
                int type,int session,void *data,size_t sz) {
    if (source == 0) {
        source = ctx->handle;
    }
    
    uint32_t dst = 0;
    if (address[0] == ':') {
        dst = strtoul(address+1, NULL, 16);
    } else if(address[0] == '.') {
        dst = mtask_handle_findname(address+1);
        if (dst==0) {
            if (type & PTYPE_TAG_DONT_COPY) {
                mtask_free(data);
            }
            return -1;
        }
    } else {
        _filter_args(ctx, type, &session, (void **)&data, &sz);
        
        struct remote_message *rmsg = mtask_malloc(sizeof(*rmsg));
        copy_name(rmsg->destination.name,address);
        rmsg->destination.handle = 0;
        rmsg->message = data;
        rmsg->sz = sz;
        
        mtask_harbor_send(rmsg, source, session);
        return session;
    }
    return mtask_send(ctx, source, dst, type, session, data, sz);
}


/**
 *  发送消息(目标是数字地址);消息推送和远程调用:有了处理别的服务发送过来的请求的能力,势必就该有向其他服务发送消息或请求的能力
 *
 *  每个(C/Lua)服务,最重要的职责就是处理别的服务发送过来的消息,以及向别的服务发送消息。每条消息由五个元素构成。
 *
 *  @param context     服务容器 context
 *
 *  @param source      消息源.每个服务都由一个 32bit 整数标识;这个整数可以看成是服务在server系统中的地址.
 *
 *                     即使在服务退出后,新启动的服务通常也不会使用已用过的地址(除非发生回绕,但一般间隔时间非常长).
 *
 *                     每条收到的消息都携带有source,方便在回应的时候可以指定地址.但地址的管理通常由框架完成.
 *
 *
 *  @param destination 目的(服务)地址
 *
 *  @param type        消息类别;每个服务可以接收256(8bit)种不同类别的消息;每种类别可以有不同的消息编码格式;有十几种类别("lua","text","client")是框架保留的;
 *
 *                     通常也不建议用户定义新的消息类别;因为用户完全可以利用已有的类别,而用具体的消息内容来区分每条具体的含义;框架把这些type映射为字符串便于记忆.
 *
 *                     最常用的消息类别名为 "lua" 广泛用于用 lua 编写的服务间的通讯.
 *
 *  @param session     大部分消息工作在请求回应模式下.即:一个服务向另一个服务发起一个请求,而后收到请求的服务在处理完请求消息后,回复一条消息;
 *
 *                     session是由发起请求的服务生成的,对它自己唯一的消息标识;回应方在回应时:将session带回.这样发送方才能识别出哪条消息是针对哪条的回应.
 *
 *                     session 是一个非负整数,当一条消息不需要回应时,使用0这个特殊的 session号.session由server框架生成管理.
 *
 *
 *  @param data        消息的C指针;在Lua层看来是一个lightuserdata;框架会隐藏这个细节,最终用户处理的是经过解码过的lua对象.
 
 *  @param sz          消息的长度;通常和message一起结合起来使用.
 *
 *  @return  -1代表消息包太大;非-1都是session id
 */
int
mtask_send(struct mtask_context *ctx,uint32_t source,uint32_t dst,int type,int session,void *data,size_t sz) {
    if((sz & HANDLE_MASK) != sz) {
        mtask_error(ctx, "message to %x is too large (sz = %lu)",dst,sz);
        mtask_free(data);
        return -1;
    }
    _filter_args(ctx, type, &session, (void**)&data, &sz);
    
    if (source == 0) {
        source = ctx->handle;
    }
    
    if (dst == 0) {
        return session;
    }
    
    if (mtask_harbor_message_isremote(dst)) {
        struct remote_message *rmsg = mtask_malloc(sizeof(*rmsg));
        rmsg->destination.handle = dst;
        rmsg->message = data;
        rmsg->sz = sz;
        mtask_harbor_send(rmsg, source, session);
    } else {
        struct mtask_message smsg;
        smsg.source = source;
        smsg.session = session;
        smsg.data = data;
        smsg.sz = sz;
        
        if (mtask_context_push(dst, &smsg)) {
            mtask_free(data);
            return -1;
        }
    }
    
    return session;
}


void
mtask_context_send(struct mtask_context *ctx,void *msg,size_t sz,uint32_t source,int type,int session) {
    struct mtask_message smsg;
    smsg.source = source;
    smsg.session = session;
    smsg.data = msg;
    smsg.sz = sz | type << HANDLE_REMOTE_SHIFT;
    
    mtask_mq_push(ctx->queue, &smsg);
}

void
mtask_callback(struct mtask_context *ctx,void*ud,mtask_cb cb) {
    ctx->cb = cb;
    ctx->cb_ud = ud;
}

void
mtask_context_reserve(struct mtask_context *ctx) {
    mtask_context_grab(ctx);
    context_dec();
}


uint32_t
mtask_queryname(struct mtask_context *ctx,const char *name) {
    switch (name[0]) {
        case ':':
            return strtol(name+1, NULL, 16);
        case '.':
            return mtask_handle_findname(name+1);
    }
    mtask_error(ctx, "Dont't support query global name %s,",name);

    return 0;
}


int
mtask_isremote(struct mtask_context *ctx,uint32_t handle,int *harbor) {
    int ret = mtask_harbor_message_isremote(handle);
    if (harbor) {
        *harbor = (int)(handle >> HANDLE_REMOTE_SHIFT);
    }
    return ret;
}




