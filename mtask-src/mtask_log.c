//
//  mtask_log.c
//  mtask
//
//  Created by TTc on 14/8/3.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <string.h>
#include <time.h>

#include "mtask_log.h"
#include "mtask.h"
#include "mtask_timer.h"
#include "mtask_socket.h"


FILE *
mtask_log_open(struct mtask_context *ctx,uint32_t handle) {
    const char *logpath = mtask_getenv("logpath");
    if(logpath == NULL) return NULL;
        
    size_t sz = strlen(logpath);
    char tmp[sz + 16];
    sprintf(tmp, "%s/%08x.log",logpath,handle);
    
    FILE *f = fopen(tmp, "ab");
    if(f) {
        uint32_t starttime = mtask_gettimer_fixsec();
        uint32_t currenttime = mtask_gettime();
        time_t ti = starttime + currenttime/100;
        mtask_error(ctx, "Open log file %s", tmp);
        fprintf(f, "open time: %u %s", currenttime, ctime(&ti));
        fflush(f);
    } else {
        mtask_error(ctx, "Open log file %s fail", tmp);
    }
    return f;
}

void mtask_log_close(struct mtask_context *ctx,FILE *f,uint32_t handle);

void mtask_log_output(FILE *f,uint32_t src,int type,int session,void *buffer,size_t sz);
