//
//  mtask_lua_mtask.c
//  mtask
//
//  Created by TTc on 14/8/10.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <lua.h>
#include <lauxlib.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "mtask_lua_seri.h"
#include "mtask.h"

#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"

struct snlua {
    lua_State *L;
    struct mtask_context *ctx;
    const char *preload;
};

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

static const char *
get_dst_string(lua_State *L,int index) {
    const char *dst_string = lua_tostring(L, index);
    if (dst_string == NULL) {
        luaL_error(L, "dst address type (%s) must be a string  or number.",lua_typename(L, lua_type(L,index)));
    }
    return dst_string;
}

static int
_send(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    uint32_t dst = (uint32_t)lua_tointeger(L, 1);
    const char *dst_string = NULL;
    if (dst == 0) {
        dst_string = get_dst_string(L, 1);
    }
    
    int type = luaL_checkinteger(L, 2);
    int session = 0;
    
    if (lua_isnil(L, 3)) {
        type |= PTYPE_TAG_ALLOC_SESSION;
    } else {
        session = luaL_checkinteger(L, 3);
    }
    
    int mtype = lua_type(L, 4);
    
    switch (mtype) {
        case LUA_TSTRING: {
            size_t len =0;
            void *msg =(void*)lua_tolstring(L, 4, &len);
            if (len == 0) {
                msg = NULL;
            }
            if (dst_string) {
                session = mtask_send_name(ctx, 0, dst_string, type, session, msg, len);
            } else {
                session = mtask_send(ctx, 0, dst, type, session, msg, len);
            }
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            void *msg = lua_touserdata(L, 4);
            int size = luaL_checkinteger(L, 5);
            if (dst_string) {
                session = mtask_send_name(ctx, 0, dst_string, type | PTYPE_TAG_DONT_COPY, session, msg, size);
            } else {
                session = mtask_send(ctx, 0, dst, type | PTYPE_TAG_DONT_COPY, session, msg, size);
            }
            break;
        }
            
        default:
            luaL_error(L, "mtask.send invalid param %s", lua_typename(L, lua_type(L,4)));
    }
    if (session<0) {
        return 0; /*send to invalid address;todo: maybe throw an error would be better*/
    }
    lua_pushinteger(L, session);
    return 1;
}

static int
_genid(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    int session = mtask_send(ctx, 0, 0, PTYPE_TAG_ALLOC_SESSION, 0, NULL, 0);
    lua_pushinteger(L, session);
    return 1;
}
/*重定向*/
static int
_redirect(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    uint32_t dst = (uint32_t)lua_tointeger(L, 1);
    const char *dst_string = NULL;
    if (dst == 0) {
        dst_string = get_dst_string(L, 1);
    }
    uint32_t source =(uint32_t)luaL_checkinteger(L, 2);
    int type = luaL_checkinteger(L, 3);
    int session = luaL_checkinteger(L, 4);
    
    int mtype = lua_type(L, 5);
    switch (mtype) {
        case LUA_TSTRING: {
            size_t len=0;
            void *msg =(void*)lua_tolstring(L, 5, &len);
            if (len == 0) {
                msg =NULL;
            }
            if (dst_string) {
                session = mtask_send_name(ctx, source, dst_string, type, session, msg, len);
            } else {
                session = mtask_send(ctx, source, dst, type, session, msg, len);
            }
            break;
        }
        default:
            luaL_error(L, "mtask.redirect invalid param %s", lua_typename(L,mtype));

    }
    return 0;
}

static int
_command(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    const char *cmd = luaL_checkstring(L, 1);
    const char *result;
    const char *parm = NULL;
    if (lua_gettop(L)==2) {
        parm = luaL_checkstring(L, 2);
    }
    
    result = mtask_command(ctx, cmd, parm);
    if (result) {
        lua_pushstring(L, result);
        return 1;
    }
    return 0;
}

static int
_error(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    mtask_error(ctx, "%s",luaL_checkstring(L, 1));
    return 0;
}

static int
_tostring(lua_State *L) {
    if (lua_isnoneornil(L, 1)) {
        return 0;
    }
    char *msg = lua_touserdata(L, 1);
    int sz = luaL_checkinteger(L, 2);
    
    lua_pushlstring(L, msg, sz);
    return 1;
}

static int
_harbor(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    uint32_t handle= (uint32_t)luaL_checkinteger(L, 1);
    int harbor =0;
    int remote = mtask_isremote(ctx, handle, &harbor);
    
    lua_pushinteger(L, harbor);
    lua_pushboolean(L, remote);
    
    return 2;
}

static int
lpackstring(lua_State *L) {
    _luaseri_pack(L);
    char *str = (char *)lua_touserdata(L, -2);
    int sz = lua_tointeger(L, -1);
    lua_pushlstring(L, str, sz);
    mtask_free(str);
    return 1;
}

static int
ltrash(lua_State *L) {
    int t = lua_type(L, 1);
    switch (t) {
        case LUA_TSTRING:{
            break;
        }
        case LUA_TLIGHTUSERDATA: {
            void *msg =lua_touserdata(L, 1);
            luaL_checkinteger(L, 2);
            mtask_free(msg);
            break;
        }
            
        default:
            luaL_error(L, "mtask.trash invalid param %s", lua_typename(L,t));
    }
    return 0;
}


static int
_cb(struct mtask_context *ctx,void *ud,int type, int session, uint32_t source, const void * msg, size_t sz) {
    lua_State *L =ud;
    int trace = 1;
    int r;
    int top = lua_gettop(L);
    if (top == 0) {
        lua_pushcfunction(L, traceback);
        lua_rawgetp(L, LUA_REGISTRYINDEX, _cb);
    } else {
        assert(top == 2);
    }
    
    lua_pushvalue(L, 2);
    
    lua_pushinteger(L, type);
    lua_pushlightuserdata(L, (void*)msg);
    lua_pushinteger(L, sz);
    lua_pushinteger(L, session);
    lua_pushinteger(L, source);
    
    r = lua_pcall(L, 5, 0, trace);
    
    if (r == LUA_OK) {
        return 0;
    }
    
    const char *self = mtask_command(ctx, "REG", NULL);
    switch (r) {
        case LUA_ERRRUN:
            mtask_error(ctx, "lua call [%x to %s : %d msgsz = %d] error : " KRED "%s" KNRM, source , self, session, sz, lua_tostring(L,-1));
            break;
        case LUA_ERRMEM:
            mtask_error(ctx, "lua memory error : [%x to %s : %d]", source , self, session);
            break;
        case LUA_ERRERR:
            mtask_error(ctx, "lua error in error : [%x to %s : %d]", source , self, session);
            break;
        case LUA_ERRGCMM:
            mtask_error(ctx, "lua gc error : [%x to %s : %d]", source , self, session);
            break;
    };
    
    lua_pop(L,1);
    
    return 0;
}

static int
forward_cb(struct mtask_context *ctx,void * ud, int type, int session, uint32_t source, const void * msg, size_t sz) {
    _cb(ctx, ud, type, session, source, msg, sz);
    /*don't delete msg in forward mode*/
    return 1;
}

static int
_callback(lua_State *L) {
    struct mtask_context *ctx = lua_touserdata(L, lua_upvalueindex(1));
    int forward = lua_toboolean(L, 2);
    luaL_checktype(L,1,LUA_TFUNCTION);
    lua_settop(L,1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, _cb);
    
    lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
    lua_State *gL = lua_tothread(L,-1);
    
    if (forward) {
        mtask_callback(ctx, gL, forward_cb);
    } else {
        mtask_callback(ctx, gL, _cb);
    }
    
    return 0;

}

int
luaopen_mtask_core(lua_State *L) {
    luaL_checkversion(L);
    
    luaL_Reg l[] = {
        { "send" , _send },
        { "genid", _genid },
        { "redirect", _redirect },
        { "command" , _command },
        { "error", _error },
        { "tostring", _tostring },
        { "pack", _luaseri_pack },
        { "unpack", _luaseri_unpack },
        { "packstring", lpackstring },
        { "trash" , ltrash },
        { "callback", _callback },
        {NULL,NULL},
    };
    
    luaL_newlibtable(L, l);
    
    lua_getfield(L, LUA_REGISTRYINDEX, "mtask_context");
    struct mtask_context *ctx = lua_touserdata(L, -1);
    if (ctx == NULL) {
        return luaL_error(L, "Init mtask context first");
    }
    luaL_setfuncs(L, l, 1);
    
    return 1;
}
