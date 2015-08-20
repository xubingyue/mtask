local mtask = require "mtask"

local max_client = 64

local server_port = 8888

mtask.start(function() 
	print("server start...")
	local console = mtask.newservice("console")
	mtask.newservice("debug_console",8000)
	mtask.newservice("heartbeat")
	local watchdog = mtask.newservice("watchdog")
	mtask.call(watchdog,"lua","start",{
		port = server_port,
		max_client = max_client,
		nodelay = true,
	})
	print("watchdog listen on: ", server_port)
	mtask.exit()
end)