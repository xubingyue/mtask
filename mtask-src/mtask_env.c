//
//  mtask_env.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//
#include "mtask.h"
#include "mtask_env.h"
#include "mtask_spinlock.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>
/**
 *  set & get Lua env
 */
struct mtask_env {
    struct spinlock lock;
    lua_State *L;   /*lua vm*/
};


static struct mtask_env *E = NULL;




/**
 *  ket = value
 *
 *  @param key key
 *
 *  @return val
 */
const char *
mtask_getenv(const char *key) {
    SPIN_LOCK(E)
    
    lua_State *L = E->L;
    
    lua_getglobal(L, key);
    const char *result = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    SPIN_UNLOCK(E)
    
    return result;
}
/**
 *  set lua env value
 *
 *  @param key   key
 *  @param value val
 */
void
mtask_setenv(const char *key,const char *value) {
    SPIN_LOCK(E)
    
    lua_State *L = E->L;
    lua_getglobal(L, key);
    assert(lua_isnil(L, -1));
    lua_pop(L, 1);
    lua_pushstring(L, value);
    lua_setglobal(L, key);
    
    SPIN_UNLOCK(E)
}

/**
 *      init the handle of lua
 */
void
mtask_env_init() {
    E = mtask_malloc(sizeof(*E));
    SPIN_INIT(E)
    E->L = luaL_newstate();
}
