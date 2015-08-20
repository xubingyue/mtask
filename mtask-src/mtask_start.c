//
//  mtask_start.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>

#include "mtask.h"
#include "mtask_server.h"
#include "mtask_imp.h"
#include "mtask_module.h"
#include "mtask_monitor.h"
#include "mtask_harbor.h"
#include "mtask_handle.h"
#include "mtask_mq.h"
#include "mtask_timer.h"
#include "mtask_socket.h"
#include "mtask_daemon.h"

/*
 为了防止竞争,条件锁的使用总是和一个互斥锁结合在一起.
 条件锁是用来线程通讯的,但是互斥锁是为了保护这种通讯不会产生逻辑错误,可以正常工作.
 */
struct monitor {
    int count;
    struct mtask_monitor **tm;
    pthread_cond_t cond; /*cond lock*/
    pthread_mutex_t mutex;/*mutex lock*/
    int sleep;          /*numbers of sleep threads*/
    int quit;
};

struct worker_parm {
    struct monitor *m;
    int id;
    int weight;  /*the  weight*/
};


#define CHECK_ABORT if(mtask_context_total()==0) break;



static void
create_thread(pthread_t *thread,void *(*start_routine) (void*),void *arg) {
    if(pthread_create(thread, NULL, start_routine, arg)) {
        fprintf(stderr, "Create thread failed");
        exit(1);
    }
}

static void
wakeup(struct monitor *m,int busy) {
     //如果睡眠线程数大于等于总工作线程数-busy(代表需要保持忙的线程数)
    if(m->sleep >= m->count - busy) {
		// signal sleep worker, "spurious wakeup" is harmless
        pthread_cond_signal(&m->cond);
    }
}



/*socket threads function*/
static void *
thread_socket(void *p) {
    struct  monitor *m = p;
    mtask_initthread(THREAD_SOCKET);
    for(;;) {
        int r = mtask_socket_poll();
        if (r==0)break;
        if (r<0) {
            CHECK_ABORT
            continue;
        }
        wakeup(m, 0);
    }
    return NULL;
}

/*release all mtask_monitor */
static void
free_monitor(struct monitor *m) {
    int i ;
    int n = m->count;
    for (i=0; i<n; i++) {
        mtask_monitor_delete(m->tm[i]);
    }
    pthread_mutex_destroy(&m->mutex);
    pthread_cond_destroy(&m->cond);
    mtask_free(m->tm);
    mtask_free(m);
}
static void *
thread_monitor(void *p) {
    struct monitor *m = p;
    int i;
    int n = m->count;
    mtask_initthread(THREAD_MONITOR);
    for (;;) {
        CHECK_ABORT
        for (i=0;i<n; i++) {
            mtask_monitor_check(m->tm[i]);
        }
        for (i=0; i<5; i++) {
            CHECK_ABORT
            sleep(1);
        }
    }
    return NULL;
}
static void *
thread_timer(void *p) {
    struct monitor *m = p;
    mtask_initthread(THREAD_TIMER);
    for (;;) {
        mtask_updatetime();
        CHECK_ABORT
        
        //唤醒休眠线程,只要有一个睡眠线程就唤醒,让工作线程热起来
        wakeup(m,m->count-1);
        usleep(2500);
    }
    // signal sleep worker, "spurious wakeup" is harmless
    mtask_socket_exit();
// wakeup all worker thread	
    pthread_mutex_lock(&m->mutex);
    m->quit = 1;
    pthread_cond_broadcast(&m->cond);
    pthread_mutex_unlock(&m->mutex);
    
    return NULL;
}
/*worker threads: used to dispatch msg(get the msg of global message queue)*/
static void *
thread_worker(void *p) {
    struct worker_parm *wp = p;
    int id = wp->id;
    int weight = wp->weight;
    struct monitor *m = wp->m;
    struct mtask_monitor *tm = m->tm[id];
    
    mtask_initthread(THREAD_WORKER);
    
    struct message_queue *q = NULL;
    while (!m->quit) {
        q = mtask_context_message_dispatch(tm, q, weight);
        if (q==NULL) {
            if (pthread_mutex_lock(&m->mutex) == 0) {
				++ m->sleep;		
			    // "spurious wakeup" is harmless,
				// because skynet_context_message_dispatch() can be call at any time.
				
                if (!m->quit) {
                    pthread_cond_wait(&m->cond, &m->mutex);
                }
                --m->sleep;
                // spurious wakeup虚假唤醒 是无害的,
                // 因为 mtask_context_message_dispatch() 可以在任何时候被调用.
                // 在调用pthread_cond_wait()前必须由本线程加锁（pthread_mutex_lock()）
                // 而在更新条件等待队列以前,mutex保持锁定状态,并在线程挂起进入等待前解锁(其他线程就可以继续使用互斥锁了)
                // 在条件满足从而离开pthread_cond_wait()之前,mutex将被重新加锁,以与进入pthread_cond_wait()前的加锁动作对应
                // 所以下面的代码中还有一个解锁的动作
                // 激发条件有两种形式,pthread_cond_signal()激活一个等待该条件的线程,存在多个等待线程时按入队顺序激活其中一个;而pthread_cond_broadcast()则激活所有等待线程。
                if (pthread_mutex_unlock(&m->mutex)) {
                    fprintf(stderr, "unlock mutex error");
                    exit(1);
                }
            }
        }
    }
    return NULL;
}





static void
start(int thread) {
    pthread_t pid[thread+3];
    
    struct monitor *m = mtask_malloc(sizeof(*m));
    memset(m, 0, sizeof(*m));
    m->count = thread;
    m->sleep = 0;

    m->tm = mtask_malloc(thread * sizeof(struct mtask_monitor *));
    
    int i ;
    for (i=0; i<thread; i++) {
        m->tm[i] = mtask_monitor_new();
    }
    if(pthread_mutex_init(&m->mutex, NULL)) {
        fprintf(stderr, "Init mutex error");
        exit(1);
    }
    if(pthread_cond_init(&m->cond, NULL)) {
        fprintf(stderr, "Init cond error");
        exit(1);
    }
    
    create_thread(&pid[0],thread_monitor,m);
    create_thread(&pid[1],thread_timer,m);
    create_thread(&pid[2],thread_socket, m);
    
    static int weight[] = {
        -1,-1,-1,-1,0,0,0,0,
        1,1,1,1,1,1,1,1,
        2,2,2,2,2,2,2,2,
        3,3,3,3,3,3,3,3,
    };
    
    struct worker_parm wp[thread];
    
    for (i=0; i<thread; i++) {
        wp[i].m = m;
        wp[i].id = i;
        
        if(i < sizeof(weight)/sizeof(weight[0])) { /* set curr thread weight when i< 32 */
            wp[i].weight = weight[i];
        } else {
            wp[i].weight = 0;                   /* set 0 when i > 32 */
        }
        create_thread(&pid[i+3], thread_worker, &wp[i]);
    }
    
    for (i=0; i<thread+3; i++) {   /*wait threads exit*/
        pthread_join(pid[i], NULL);
    }
    free_monitor(m);
}

static void
bootstrap(struct mtask_context *logger,const char *cmdline) {
    int sz = strlen(cmdline);
    char name[sz+1];
    char args[sz+1];
    sscanf(cmdline, "%s %s",name,args);
    
    struct mtask_context *ctx = mtask_context_new(name, args);
    if(ctx == NULL) {
        mtask_error(NULL, "Bootstrap error: %s\n",cmdline);
        mtask_context_dispatchall(logger);
        exit(1);
    }
}
void
mtask_start(struct mtask_config *config) {
    if(config->daemon) {
        if(daemon_init(config->daemon)) {
            exit(1);
        }
    }
    /*init all 组件*/
    mtask_harbor_init(config->harbor);
    mtask_handle_init(config->harbor);
    mtask_mq_init();
    mtask_module_init(config->module_path);
    mtask_timer_init();
    mtask_socket_init();
    /*start first C service : log service*/
    struct mtask_context *ctx = mtask_context_new(config->logservice,config->logger);
    
    if(ctx == NULL) {
        fprintf(stderr, "Can't launch %s service \n",config->logservice);
        exit(1);
    }
    /*start second service(the first Lua service) :bootstrap*/
    bootstrap(ctx, config->bootstrap);
    
    start(config->thread);
    
    mtask_harbor_exit();
    mtask_socket_free();
    
    if (config->daemon) {
        daemon_exit(config->daemon);
    }
}