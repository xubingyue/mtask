//
//  mtask_env.c
//  mtask
//
//  Created by TTc on 14/9/31.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#include "mtask_env.h"

#include <lua.h>
#include <lauxlib.h>

#include <stdlib.h>
#include <assert.h>
/**
 *  set & get Lua env
 */
struct mtask_env {
    int lock;
    lua_State *L;   /*lua vm*/
};


static struct mtask_env *E = NULL;

/*get the lock*/
#define LOCK(q) while(__sync_lock_test_and_set(&(q)->lock,1)){}
/*release the lock*/
#define UNLOCK(q) __sync_lock_release(&(q)->lock);

/**
 *      init the handle of lua
 */
void
mtask_env_init() {
    E = malloc(sizeof(*E));
    E->lock = 0;
    E->L = luaL_newstate();
}
/**
 *  ket = value
 *
 *  @param key key
 *
 *  @return val
 */
const char *
mtask_getenv(const char *key) {
    LOCK(E);
    
    lua_State *L = E->L;
    
    lua_getglobal(L, key);
    const char *result = lua_tostring(L, -1);
    lua_pop(L, 1);
    
    UNLOCK(E);
    
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
    LOCK(E)
    
    lua_State *L = E->L;
    lua_getglobal(L, key);
    assert(lua_isnil(L, -1));
    lua_pop(L, 1);
    lua_pushstring(L, value);
    lua_setglobal(L, key);
    
    UNLOCK(E)
}


