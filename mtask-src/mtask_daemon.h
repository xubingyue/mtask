//
//  mtask_daemon.h
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_daemon__
#define __mtask__mtask_daemon__

int daemon_init(const char *pidfile);

int daemon_exit(const char *pidfile);

#endif /* defined(__mtask__mtask_daemon__) */
