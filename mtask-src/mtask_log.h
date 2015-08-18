//
//  mtask_log.h
//  mtask
//
//  Created by TTc on 14/8/3.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_log__
#define __mtask__mtask_log__

#include <stdio.h>
#include <stdint.h>

#include "mtask.h"
#include "mtask_env.h"

FILE *mtask_log_open(struct mtask_context *ctx,uint32_t handle);

void mtask_log_close(struct mtask_context *ctx,FILE *f,uint32_t handle);

void mtask_log_output(FILE *f,uint32_t src,int type,int session,void *buffer,size_t sz);

#endif /* defined(__mtask__mtask_log__) */
