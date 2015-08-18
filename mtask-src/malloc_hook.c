//
//  malloc_hook.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "malloc_hook.h"
#include "mtask.h"

static size_t _used_memory = 0;
static size_t _memory_block = 0;

typedef struct _mem_data {
    uint32_t handle;
    ssize_t allocated;
}mem_data;

#define  SLOT_SIZE 0x10000   //65536

#define PREFIX_SIZE sizeof(uint32_t)

static mem_data mem_stats[SLOT_SIZE];

void
memory_info_dump(void) {
    mtask_error(NULL, "No jemalloc");
}

size_t
mallctl_int64(const char* name, size_t* newval) {
    mtask_error(NULL, "No jemalloc : mallctl_int64 %s.", name);
    return 0;
}

int
mallctl_opt(const char* name, int* newval) {
    mtask_error(NULL, "No jemalloc : mallctl_opt %s.", name);
    return 0;
}
size_t
malloc_used_memory(void) {
    return _used_memory;
}

size_t
malloc_memory_block(void) {
    return _memory_block;
}

void
dump_c_mem() {
    int i;
    size_t total = 0;
    mtask_error(NULL, "dump all service mem:");
    for(i=0; i<SLOT_SIZE; i++) {
        mem_data* data = &mem_stats[i];
        if(data->handle != 0 && data->allocated != 0) {
            total += data->allocated;
            mtask_error(NULL, "0x%x -> %zdkb", data->handle, data->allocated >> 10);
        }
    }
    mtask_error(NULL, "+total: %zdkb",total >> 10);
}
/**
 *  字符串复制(辅助函数)
 *
 *  @param str 待操作的字符串
 *
 *  @return //返回新串的地址
 */
char *
mtask_strdup(const char *str) {                                 //将串拷贝到新建的位置处
    size_t sz = strlen(str);                                     //字符串大小
    char * ret = mtask_malloc(sz+1);
    memcpy(ret, str, sz+1);                                      //拷贝数据
    return ret;                                                  //返回新串的地址
}

void *
mtask_lalloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    if (nsize == 0) {
        mtask_free(ptr);
        return NULL;
    } else {
        return mtask_realloc(ptr, nsize);
    }
}
