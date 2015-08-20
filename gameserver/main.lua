local skynet = require "skynet"

local max_client = 64

local server_port = 8888

skynet.start(function() 
	print("server start...")
	local console = skynet.newservice("console")
	skynet.newservice("debug_console",8000)
	skynet.newservice("heartbeat")
	local watchdog = skynet.newservice("watchdog")
	skynet.call(watchdog,"lua","start",{
		port = server_port,
		max_client = max_client,
		nodelay = true,
	})
	print("watchdog listen on: ", server_port)
	skynet.exit()
end)