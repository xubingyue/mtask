//
//  mtask_handle.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_handle__
#define __mtask__mtask_handle__

#include <stdint.h>

#include "mtask_harbor.h"

struct mtask_context;

void mtask_handle_init(int harbor);

uint32_t mtask_handle_register(struct mtask_context *ctx);

int mtask_handle_retire(uint32_t handle);

struct mtask_context * mtask_handle_grab(uint32_t handle);

uint32_t mtask_handle_findname(const char *name);



#endif /* defined(__mtask__mtask_handle__) */
