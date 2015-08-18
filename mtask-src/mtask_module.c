//
//  mtask_module.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include "mtask.h"
#include "mtask_module.h"


#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <dlfcn.h>

#define MAX_MODULE_TYPE 32

/*(The dynamic library) manager of all module */
struct modules {
    int count;
    int lock;
    const char *path;                      /*待装载的模块目录*/
    struct mtask_module m[MAX_MODULE_TYPE];/*存放装载的模块*/
};

static struct modules *M = NULL;    /*global*/

/*check module load*/
static struct mtask_module *
_query(const char *name) {
    int i;
    for (i=0; i<M->count; i++) {
        if(strcmp(M->m[i].name, name) == 0) {
            return &M->m[i];
        }
    }
    return NULL;
}
/*load the dynamic library*/
static void *
_try_open(struct modules *m,const char *name) {
    const char *l;
    const char *path = m->path;
    size_t path_size = strlen(path);
    size_t name_size = strlen(name);
    
    int sz = path_size + name_size;
    /*search path*/
    void *dl = NULL;
    char tmp[sz];
    do {
        memset(tmp, 0, sz);
        while (*path == ';') path++;
        if(*path == '\0')break;
        l = strchr(path, ';');
        if(l == NULL) l = path + strlen(path);
        int len = l - path;
        int i;
        for (i=0;path[i]!='?' && i < len ;i++) {
            tmp[i] = path[i];
        }
        memcpy(tmp+i,name,name_size);
        if (path[i] == '?') {
            strncpy(tmp+i+name_size,path+i+1,len - i - 1);
        } else {
            fprintf(stderr,"Invalid C service path\n");
            exit(1);
        }
        /*立即解析该库， 且其后的函数能使用该库的函数*/
        dl = dlopen(tmp, RTLD_NOW | RTLD_GLOBAL);
        path = l;
    }while (dl == NULL);
    
    
    if(dl == NULL) {
        fprintf(stderr, "try open %s failed : %s\n",name,dlerror());
    }
    return dl;
}
/* set the dynamic library  (create/init/release/signal) callback function*/
static int
_open_sym(struct mtask_module *mod) {
    size_t name_size = strlen(mod->name);
    
    char tmp[name_size +9];     //create/init/release/signal,(7)分配足够的缓冲区

    memcpy(tmp, mod->name, name_size);
    
    strcpy(tmp+name_size, "_create");
    mod->create = dlsym(mod->module, tmp);
    
    strcpy(tmp+name_size, "_init");
    mod->init =dlsym(mod->module, tmp);
    
    strcpy(tmp+name_size, "_release");
    mod->release = dlsym(mod->module,tmp);
    
    strcpy(tmp+name_size, "_signal");
    mod->signal = dlsym(mod->module, tmp);
    
    return mod->init == NULL;
    
    
}

void
mtask_module_init(const char *path) {
    struct modules *m = malloc(sizeof(*m));
    
    m->count = 0;
    m->path = mtask_strdup(path);
    m->lock = 0;
    
    M = m;
}


void
mtask_module_insert(struct mtask_module *mod) {
   	while(__sync_lock_test_and_set(&M->lock,1)) {}
    
    struct mtask_module * m = _query(mod->name);
    assert(m == NULL && M->count < MAX_MODULE_TYPE);
    int index = M->count;
    M->m[index] = *mod;
    ++M->count;
    __sync_lock_release(&M->lock);
}
/*create  Lua module callback function */
void *
mtask_module_instance_create(struct mtask_module*m) {
    if(m->create) return m->create();
    else return (void *)(intptr_t)(~0);
}
/*init  Lua module callback function */
int
mtask_module_instance_init(struct mtask_module * m,void * inst,struct mtask_context *ctx,
                           const char *parm) {
    return m->init(inst,ctx,parm);
}
/*release  Lua module callback function */
void
mtask_module_instance_release(struct mtask_module *m,void *inst) {
    if(m->release) {
        m->release(inst);
    }
}
/*signal  Lua module callback function */
void
mtask_module_instance_signal(struct mtask_module *m,void *inst,int signal) {
    if(m->signal) {
        m->signal(inst,signal);
    }
}

struct mtask_module *
mtask_module_query(const char *name) {
    struct mtask_module * result = _query(name);
    if (result) return result;
    
    while(__sync_lock_test_and_set(&M->lock,1)) {}

    result = _query(name);
    
    if(result == NULL  && M->count < MAX_MODULE_TYPE) {
        int index = M->count;
        
        void *dl = _try_open(M, name);
        if(dl) {
            M->m[index].name = name;
            M->m[index].module = dl;
            if(_open_sym(&M->m[index]) == 0) {
                M->m[index].name = mtask_strdup(name);
                M->count++;
                result = &M->m[index];
            }
        }
    }
    __sync_lock_release(&M->lock);

    return result;
}