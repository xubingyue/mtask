//
//  mtask_main.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <signal.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include "mtask.h"
#include "mtask_imp.h"
#include "mtask_env.h"
#include "mtask_server.h"


static const char * load_config = "\
    local config_name = ...\
    local f = assert(io.open(config_name))\
    local code = assert(f:read \'*a\')\
    local function getenv(name) return assert(os.getenv(name), name) end\
    code = string.gsub(code, \'%$([%w_%d]+)\', getenv)\
    f:close()\
    local result = {}\
    assert(load(code,\'=(load)\',\'t\',result))()\
    return result\
";




static int
optint(const char *key,int opt) {
    const char *str = mtask_getenv(key);
    if(str == NULL) {
        char tmp[20];
        sprintf(tmp,"%d",opt);
        mtask_setenv(key, tmp);
        return opt;
    }
    return strtol(str, NULL, 10);
}

static const char *
optstring(const char *key,const char *opt) {
    const char *str = mtask_getenv(key);
    if(str == NULL) {
        if(opt) {
            mtask_setenv(key, opt);
            opt = mtask_getenv(key);
        }
        return opt;
    }
    return str;
}

static void
_init_env(lua_State *L) {
    lua_pushnil(L);
    while (lua_next(L, -2)!=0) {
        int keyt = lua_type(L, -2);
        if(keyt != LUA_TSTRING) {
            fprintf(stderr, "Invalid config table\n");
            exit(1);
        }
        const char *key = lua_tostring(L, -2);
        if(lua_type(L, -1) == LUA_TBOOLEAN) {
            int b = lua_toboolean(L, -1);
            mtask_setenv(key, b ? "true" : "fasle");
        } else {
            const char *value = lua_tostring(L, -1);
            if(value == NULL) {
                fprintf(stderr, "Invalid table \n");
                exit(1);
            }
            mtask_setenv(key, value);
        }
        lua_pop(L, 1);
    }
    lua_pop(L, 1);
}

int
sigign() {
    struct sigaction sa;
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, 0);
    return 0;
}

int
main(int argc, char *argv[]) {
    const char *confilg_file = NULL;
    if(argc > 1) {
        confilg_file = argv[1];
    } else {
        fprintf(stderr, "Need a config file.\n");
        return 1;
    }
    
    mtask_global_init();
    mtask_env_init();
    
    sigign();
    
    struct mtask_config config;
    
    struct lua_State *L = lua_newstate(mtask_lalloc, NULL);
    
    luaL_openlibs(L);
    
    int err = luaL_loadstring(L, load_config);
    
    assert(err == LUA_OK);
    
    lua_pushstring(L, confilg_file);
    
    err = lua_pcall(L, 1, 1, 0);
    if (err) {
        fprintf(stderr, "%s\n",lua_tostring(L, -1));
        lua_close(L);
        return 1;
    }
    
    _init_env(L);
    /* init mtask_config*/
    config.thread = optint("thread", 8);
    config.module_path = optstring("cpath", "./cservice/?.so");
    config.harbor = optint("harbor", 1);
    config.bootstrap = optstring("bootstrap", "snlua bootstrap");
    config.daemon = optstring("daemon", NULL);
    config.logger = optstring("logger", NULL);
    config.logservice = optstring("logservice", "logger");
    
    lua_close(L);
    
    mtask_start(&config);
    mtask_global_exit();
    
    return 0;
}