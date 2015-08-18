//
//  mtask_monitor.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdlib.h>
#include <string.h>

#include "mtask_monitor.h"
#include "mtask.h"
#include "mtask_server.h"


struct mtask_monitor {
    int version;        /*curr ver*/
    int check_version;  /*old ver*/
    uint32_t source;    /*source for module*/
    uint32_t destination;/*dst for module,used to check if the module in work*/
};



struct mtask_monitor *
mtask_monitor_new(){
    struct mtask_monitor *ret = mtask_malloc(sizeof(*ret));
    memset(ret, 0, sizeof(*ret));
    return ret;
}
/**  触发监视器
 *  在派发消息之前触发监视器(设置tm->source tm->destination)
 *
 *  在派发消息之后重置监视器(同样是调用mtask_monitor_trigger),为什么调用同一个函数,效果不同？因为在tm->destination为0的时候,检查监视器是毫无效果的
 *  而在派发消息之前触发了监视器,但是派发消息时陷入死循环,那么就没有重置监视器,在检查监视器的时候,就会出现
 *
 *  tm->version == tm->check_version并且tm->destination不为0了,就要报告错误了(mtask_error)
 *
 *  @param tm          监视器      handle of the monitor
 *  @param source      来源服务地址 source of the module
 *  @param destination 目的服务地址 dstination dst for the module
 */

/* watchdog for check if the module unregister*/
void
mtask_monitor_check(struct mtask_monitor *tm) {
    /*module in work, so we can check if the module uninstalled when we try to use it*/
    if(tm->version == tm->check_version) {
        if(tm->destination) {
            /*check if the module exit, and try to mark as exit*/
            mtask_context_endless(tm->destination);
            mtask_error(NULL, "a message from [ :%08x ] to [ :%08x ] maybe in an endless loop (version = %d)",tm->source , tm->destination, tm->version);
        }
    } else {  /*module maybe not in work, just update version*/
        tm->check_version = tm->version;
    }
}
/*trigger the monitor*/
void
mtask_monitor_trigger(struct mtask_monitor *m,uint32_t source,uint32_t dst) {
    m->source = source;
    m->destination = dst;
    __sync_fetch_and_add(&m->version,1);
}

void
mtask_monitor_delete(struct mtask_monitor *tm) {
    mtask_free(tm);
}



