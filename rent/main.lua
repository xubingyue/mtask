-- main.lua in rent
local mtask = require "mtask"

local max_client = 1024

local server_port = 8888

mtask.start(function() 
	print("server start...")
	local console = mtask.newservice("console")
	mtask.newservice("debug_console",8000)
	mtask.newservice("heartbeat")
	local watchdog = mtask.newservice("watchdog")
	mtask.call(watchdog,"lua","start",{
		port = server_port, 	 -- 监听端口 8888
		max_client = max_client, -- 最多允许 1024 个外部连接同时建立
		nodelay = true,			 -- 给外部连接设置  TCP_NODELAY 属性
	})
	print("watchdog listen on: ", server_port)
	mtask.exit()
end)