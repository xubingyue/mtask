//
//  mtask_lua_multicast.c
//  mtask
//
//  Created by TTc on 14/8/14.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include <lua.h>
#include <lauxlib.h>
#include <stdint.h>
#include <string.h>

#include "mtask.h"


struct mc_package {
    int reference;
    uint32_t size;
    void *data;
};

static int
pack(lua_State *L,void *data,size_t size) {
    struct mc_package *pack = mtask_malloc(sizeof(*pack));
    pack->reference = 0;
    pack->size = (uint32_t)size;
    pack->data = data;
    
    struct mc_package **ret = mtask_malloc(sizeof(*ret));
    *ret = pack;
    lua_pushlightuserdata(L, ret);
    lua_pushinteger(L, sizeof(ret));
    return 2;
}

static int
mc_packlocal(lua_State *L) {
    void *data = lua_touserdata(L, 1);
    size_t size = luaL_checkinteger(L, 2);
    if (size != (uint32_t)size) {
        return luaL_error(L, "Size should be 32bit integer");
    }
    return pack(L, data, size);
}

static int
mc_unpacklocal(lua_State *L) {
    struct mc_package **pack = lua_touserdata(L, 1);
    int sz = luaL_checkinteger(L, 2);
    if (sz != sizeof(*pack)) {
        return luaL_error(L, "Invalid multicast package size %d",sz);
    }
    lua_pushlightuserdata(L, *pack);
    lua_pushlightuserdata(L, (*pack)->data);
    lua_pushinteger(L, (lua_Integer)((*pack)->size));
    return 3;
}

static int
mc_bindrefer(lua_State *L) {
    struct mc_package ** pack = lua_touserdata(L, 1);
    int ref = luaL_checkinteger(L, 2);
    if ((*pack)->reference != 0) {
        return luaL_error(L, "Can't bind a multicast package more than once");
    }
    (*pack)->reference = ref;
    
    lua_pushlightuserdata(L, *pack);
    
    return 1;
}

static int
mc_closelocal(lua_State *L) {
    struct mc_package *pack = lua_touserdata(L, 1);
    
    int ref = __sync_sub_and_fetch(&pack->reference ,1);
    if (ref <=0) {
        mtask_free(pack->data);
        mtask_free(pack);
        if (ref<0) {
            return luaL_error(L, "Invalid multicast package ref %d",ref);
        }
    }
    
    return 0;
}

static int
mc_remote(lua_State *L) {
    struct mc_package **ptr = lua_touserdata(L, 1);
    struct mc_package *pack = *ptr;
    
    lua_pushlightuserdata(L, pack->data);
    lua_pushinteger(L, (lua_Integer)pack->size);
    
    mtask_free(pack);
    
    return 2;
}

static int
mc_packstring(lua_State *L) {
    size_t size;
    const char *msg = luaL_checklstring(L, 1, &size);
    if (size !=(uint32_t)size) {
        return luaL_error(L, "string is too long");
    }
    void *data  = mtask_malloc(size);
    memcpy(data, msg, size);
    return pack(L, data, size);
}

static int
mc_packremote(lua_State *L) {
    void *data = lua_touserdata(L, 1);
    size_t size = luaL_checkinteger(L, 2);
    if (size != (uint32_t)size) {
        return luaL_error(L, "Size should be 32bit integer");
    }
    void *msg = mtask_malloc(size);
    memcpy(msg, data, size);
    return pack(L, msg, size);
}

static int
mc_nextid(lua_State *L) {
    uint32_t id = luaL_checkinteger(L, 1);
    id += 256;
    lua_pushinteger(L, id);
    
    return 1;
}

int
luaopen_multicast_core(lua_State *L) {
    luaL_Reg l[] = {
        { "pack",   mc_packlocal},
        { "unpack", mc_unpacklocal },
        { "bind",   mc_bindrefer },
        { "close",  mc_closelocal },
        { "remote", mc_remote },
        { "packstring", mc_packstring },
        { "packremote", mc_packremote },
        { "nextid", mc_nextid },
        {NULL,NULL},
    };
    luaL_checkversion(L);
    luaL_newlib(L, l);
    return 1;
}