-- main.lua in rent
local skynet = require "skynet"

local max_client = 1024

local server_port = 8888

skynet.start(function() 
	print("server start...")
	local console = skynet.newservice("console")
	skynet.newservice("debug_console",8000)
	skynet.newservice("heartbeat")
	local watchdog = skynet.newservice("watchdog")
	skynet.call(watchdog,"lua","start",{
		port = server_port, 	 -- 监听端口 8888
		max_client = max_client, -- 最多允许 1024 个外部连接同时建立
		nodelay = true,			 -- 给外部连接设置  TCP_NODELAY 属性
	})
	print("watchdog listen on: ", server_port)
	skynet.exit()
end)