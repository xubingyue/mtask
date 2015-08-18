//
//  mtask_error.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#include "mtask.h"
#include "mtask_server.h"







void
mtask_error(struct mtask_context *context,const char *msg, ...) {
    static uint32_t logger = 0;
    if (logger == 0) {
       
    }
    if(logger == 0) {
        return;
    }
}

