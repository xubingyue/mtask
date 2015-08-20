local mtask = require "mtask"

local max_client = 64

mtask.start(function()
	print("Server start")
	local console = mtask.newservice("console")
	mtask.newservice("debug_console",8000)
	mtask.newservice("simpledb")
	local watchdog = mtask.newservice("watchdog")
	mtask.call(watchdog, "lua", "start", {
		port = 9999,
		maxclient = max_client,
		nodelay = true,
	})
	print("Watchdog listen on ", 9999)

	mtask.exit()
end)
