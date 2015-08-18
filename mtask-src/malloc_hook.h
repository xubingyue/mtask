//
//  malloc_hook.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__malloc_hook__
#define __mtask__malloc_hook__

#include <stdlib.h>

extern size_t malloc_used_memory(void);

extern size_t malloc_memory_block(void);

extern void memory_info_dump(void);

extern size_t mallctl_int64(const char *name,size_t *newval);

extern int mallctl_opt(const char *name,int *newval);

extern void dump_c_mem(void);


#endif /* defined(__mtask__malloc_hook__) */
