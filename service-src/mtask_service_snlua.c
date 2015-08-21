//
//  mtask_snlua.c
//  mtask
//
//  Created by TTc on 14/8/6.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "mtask.h"

struct snlua {
    lua_State *L;
    struct mtask_context *ctx;
};

#ifdef LUA_CACHELIB

#define  codecache luaopen_cache

#else


static int
cleardummy(lua_State *L) {
    return 0;
}

/* n lua vm share one bytecode*/
static int
codecache(lua_State *L) {
    luaL_Reg l[] = {
        {"clear",cleardummy},
        {NULL,NULL},
    };
    luaL_newlib(L, l);
    lua_getglobal(L, "loadfile"); //把全局变量loadfile里的值(函数)压栈,该值目前在栈顶
    lua_setfield(L,-2,"loadfile");//新建的表["loadfile"]=栈顶的值(loadfile函数),并把栈顶的值弹出栈

    return 1;
}

#endif

/*trackback info of lua stack */
static int
traceback(lua_State *L) {
    const char *msg = lua_tostring(L, 1);
    if (msg) {
        luaL_traceback(L, L, msg, 1);
    } else {
        lua_pushliteral(L, "(no error message)");
    }
    return 1;
}

static void
_report_launcher_error(struct mtask_context *ctx) {
    mtask_send_name(ctx, 0, ".launcher", PTYPE_TEXT, 0, "ERROR", 5);
}

static const char *
optstring(struct mtask_context *ctx,const char *key,const char *str) {
    const char *ret = mtask_command(ctx, "GETENV", key);
    if (ret == NULL) {
        return str;
    }
    return ret;
}

static int
_init(struct snlua *l,struct mtask_context *ctx,const char *args,size_t sz) {
    lua_State *L = l->L;
    l->ctx = ctx;
    
    lua_gc(L, LUA_GCSTOP, 0);
    
    lua_pushboolean(L, 1);
    lua_setfield(L, LUA_REGISTRYINDEX, "LUA_NOENV");
    
    luaL_openlibs(L);
    
    lua_pushlightuserdata(L, ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, "mtask_context");
    luaL_requiref(L, "mtask.codecache", codecache, 0);
    
    lua_pop(L, 1);
    
    const char *path = optstring(ctx, "lua_path", "./lualib/?.lua;./lualib/?/init.lua");
    lua_pushstring(L, path);
    lua_setglobal(L, "LUA_PATH");

    const char *cpath = optstring(ctx, "lua_cpath", "./luaclib/?.so");
    lua_pushstring(L, cpath);
    lua_setglobal(L, "LUA_CPATH");
    
    const char *service = optstring(ctx, "luaservice", "./service/?.lua");
    lua_pushstring(L, service);
    lua_setglobal(L, "LUA_SERVICE");
    
    const char *preload = mtask_command(ctx, "GETENV", "preload");
    lua_pushstring(L, preload);
    lua_setglobal(L, "LUA_PRELOAD");
    
    
    lua_pushcfunction(L, traceback);
    assert(lua_gettop(L) == 1);
    
    
    const char *loader = optstring(ctx, "lualoader", "./lualib/loader.lua");
    
    int r = luaL_loadfile(L, loader);
    
    if (r != LUA_OK) {
        mtask_error(ctx, "can't load %s : %s",loader,lua_tostring(L, -1));
        _report_launcher_error(ctx);
        return 1;
    }
		lua_pushlstring(L, args, sz);//args为第一个发送过来的消息bootstrap,将bootstrap压栈
	//第一次启动的snlua服务,args为bootstrap,之后启动的snlua服务,args为要启动的LUA服务名字
	//每个LUA服务都由snlua来承载
	r = lua_pcall(L,1,0,1);//调用loader 以bootstrap为参数
	if (r != LUA_OK) {
		mtask_error(ctx, "lua loader error : %s", lua_tostring(L, -1));
		_report_launcher_error(ctx);
		return 1;
	}
    
    lua_settop(L, 0);
    
    lua_gc(L, LUA_GCRESTART, 0);
    return 0;
}


static int
_launch(struct mtask_context *ctx,void *ud,int type,int session,uint32_t source,const void *msg,size_t sz) {
    assert(type ==0 && session == 0);
    
    struct snlua *l = ud;
    
    mtask_callback(ctx, NULL, NULL);
    
    int err = _init(l, ctx, msg, sz);
    
    if (err) {
        mtask_command(ctx, "EXIT", NULL);
    }
    return 0;
}

int
snlua_init(struct snlua *l,struct mtask_context *ctx,const char *args) {
    int sz = strlen(args);
    char *tmp = mtask_malloc(sz);
    memcpy(tmp, args, sz);
    mtask_callback(ctx, l, _launch);
    
    const char *self = mtask_command(ctx, "REG", NULL);
    
    uint32_t handle_id = strtoul(self+1, NULL, 16);
    
    mtask_send(ctx, 0, handle_id, PTYPE_TAG_DONT_COPY,0, tmp, sz);
    
    return 0;
}

struct snlua *
snlua_create(void) {
    struct snlua *l = mtask_malloc(sizeof(*l));
    memset(l, 0, sizeof(*l));
    l->L = lua_newstate(mtask_lalloc, NULL);
    return l;
}

void
snlua_release(struct snlua *l) {
    lua_close(l->L);
    mtask_free(l);
}







