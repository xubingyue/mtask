
--main.lua


local mtask = require "mtask"
local max_client = 64


mtask.start(function() 
	print("server  start...")
	mtask.newservice("talkbox")
	local watchdog = mtask.newservice("watchdog")
	mtask.call(watchdog,"lua","start",{
				port = 10101,
				max_client = max_client,
		})
end)