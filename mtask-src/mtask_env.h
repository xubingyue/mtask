//
//  mtask_env.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_env__
#define __mtask__mtask_env__

void mtask_env_init();

const char *mtask_getenv(const char *key);

void mtask_setenv(const char *key,const char *value);



#endif /* defined(__mtask__mtask_env__) */
