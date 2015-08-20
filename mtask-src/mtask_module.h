//
//  mtask_module.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_module__
#define __mtask__mtask_module__

struct mtask_context;

typedef void * (*mtask_dl_create)(void);

typedef int (*mtask_dl_init)(void *inst,struct mtask_context *context,const char *parm);

typedef void (*mtask_dl_release)(void *inst);

typedef void (*mtask_dl_signal)(void *inst,int signal);

struct mtask_module {
    const char *name;
    void *module;           /*modlue The dynamic library*/
    mtask_dl_create create;
    mtask_dl_init  init;
    mtask_dl_release release;
    mtask_dl_signal signal;
};

void mtask_module_insert(struct mtask_module *mod);

struct mtask_module * mtask_module_query(const char *name);

void * mtask_module_instance_create(struct mtask_module*m);

int mtask_module_instance_init(struct mtask_module * m,void * inst,struct mtask_context *ctx,const char *parm);

void mtask_module_instance_release(struct mtask_module *m,void *inst);

void mtask_module_instance_signal(struct mtask_module *m,void *inst,int signal);

void mtask_module_init(const char *path);


#endif /* defined(__mtask__mtask_module__) */
