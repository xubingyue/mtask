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

// reserve high 8 bits for remote id


#define HANDLE_MASK 0xffffff
#define HANDLE_REMOTE_SHIFT 24
struct mtask_context;


uint32_t mtask_handle_register(struct mtask_context *ctx);
int mtask_handle_retire(uint32_t handle);
struct mtask_context * mtask_handle_grab(uint32_t handle);
void mtask_handle_retireall();



uint32_t mtask_handle_findname(const char *name);

const char * mtask_handle_namehandle(uint32_t handle, const char *name);


void mtask_handle_init(int harbor);

#endif /* defined(__mtask__mtask_handle__) */
