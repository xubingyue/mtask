//
//  mtask_timer.h
//  mtask
//
//  Created by TTc on 14/8/2.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_timer__
#define __mtask__mtask_timer__

#include <stdint.h>


int mtask_timeout(uint32_t handle, int time, int session);

void mtask_updatetime(void);

uint32_t mtask_gettime(void);

uint32_t mtask_gettimer_fixsec(void);

void mtask_timer_init(void);

#endif /* defined(__mtask__mtask_timer__) */
