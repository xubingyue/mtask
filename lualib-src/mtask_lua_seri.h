//
//  mtask_lua_seri.h
//  mtask
//
//  Created by TTc on 14/8/10.
//  Copyright (c) 2015年 TTc. All rights reserved.
//

#ifndef __mtask__mtask_lua_seri__
#define __mtask__mtask_lua_seri__

#include <lua.h>

int _luaseri_pack(lua_State *L);

int _luaseri_unpack(lua_State *L);

#endif /* defined(__mtask__mtask_lua_seri__) */
