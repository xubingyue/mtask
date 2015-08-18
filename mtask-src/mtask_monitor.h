//
//  mtask_monitor.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_monitor__
#define __mtask__mtask_monitor__

#include <stdint.h>

struct mtask_monitor;

struct mtask_monitor *mtask_monitor_new();

void mtask_monitor_check(struct mtask_monitor *tm);

void mtask_monitor_trigger(struct mtask_monitor *tm,uint32_t source,uint32_t dst);

void mtask_monitor_delete(struct mtask_monitor *tm);
#endif /* defined(__mtask__mtask_monitor__) */
