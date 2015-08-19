local mtask = require "mtask"
local c = require "mtask.core"
local snax_interface = require "snax.interface"
local profile = require "profile"
local snax = require "snax"

local snax_name = tostring(...)
local func, pattern = snax_interface(snax_name, _ENV)
local snax_path = pattern:sub(1,pattern:find("?", 1, true)-1) .. snax_name ..  "/"
package.path = snax_path .. "?.lua;" .. package.path

SERVICE_NAME = snax_name
SERVICE_PATH = snax_path

local profile_table = {}

local function update_stat(name, ti)
	local t = profile_table[name]
	if t == nil then
		t = { count = 0,  time = 0 }
		profile_table[name] = t
	end
	t.count = t.count + 1
	t.time = t.time + ti
end

local traceback = debug.traceback

local function do_func(f, msg)
	return xpcall(f, traceback, table.unpack(msg))
end

local function dispatch(f, ...)
	return mtask.pack(f(...))
end

local function return_f(f, ...)
	return mtask.ret(mtask.pack(f(...)))
end
--这段代码中，使用 profile.start() 和 profile.stop() 统计出其间的时间开销（返回单位是秒）。然后按消息类型分别记录在一张表 ti 中。
local function timing( method, ... )
	local err, msg
	profile.start()
	if method[2] == "accept" then
		-- no return
		err,msg = xpcall(method[4], traceback, ...)
	else
		err,msg = xpcall(return_f, traceback, method[4], ...)
	end
	local ti = profile.stop()
	update_stat(method[3], ti)
	assert(err,msg)
end

--使用 mtask.info_func() 可以注册一个函数给 debug 消息处理。向这个服务发送 debug 消息 INFO 就会调用这个函数取得返回值。
--ps. 使用 debug console 可以主动向服务发送 debug 消息。
mtask.start(function()
	local init = false
	mtask.dispatch("snax", function ( session , source , id, ...)
		local method = func[id]

		if method[2] == "system" then
			local command = method[3]
			if command == "hotfix" then
				local hotfix = require "snax.hotfix"
				mtask.ret(mtask.pack(hotfix(func, ...)))
			elseif command == "init" then
				assert(not init, "Already init")
				local initfunc = method[4] or function() end
				initfunc(...)
				mtask.ret()
				mtask.info_func(function() --注册info函数,便于debug指令INFO查询
					return profile_table
				end)
				init = true
			else
				assert(init, "Never init")
				assert(command == "exit")
				local exitfunc = method[4] or function() end
				exitfunc(...)
				mtask.ret()
				init = false
				mtask.exit()
			end
		else
			assert(init, "Init first")
			timing(method, ...)
		end
	end)
end)
