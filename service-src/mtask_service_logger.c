//
//  mtask_logger.c
//  mtask
//
//  Created by TTc on 14/8/6.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "mtask.h"

struct logger {
    FILE *handle;
    int close;
};

struct logger *
logger_create(void) {
    struct logger *log = mtask_malloc(sizeof(*log));
    log->handle = NULL;
    log->close = 0;
    return log;
}

void
logger_release(struct logger *log) {
    if (log->close) {
        fclose(log->handle);
    }
    mtask_free(log);
}
static int
_logger(struct mtask_context *ctx,void *ud,int type,int session, uint32_t source, const void * msg, size_t sz) {
    struct logger *log = ud;
    
    fprintf(log->handle, "[:%08x]",source);
    fwrite(msg, sz, 1, log->handle);
    fprintf(log->handle, "\n");
    fflush(log->handle);
    
    return 0;
}

int
logger_init(struct logger *log,struct mtask_context *ctx,const char *parm) {
    if (parm) {
        log->handle = fopen(parm, "w");
        if (log->handle == NULL) {
            return 1;
        }
        log->close = 1;
    } else {
        log->handle = stdout;
    }
    if (log->handle) {
        mtask_callback(ctx, log, _logger);
        mtask_command(ctx, "REG", ".logger");
        return 0;
    }
    return 1;
}

