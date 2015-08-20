
--main.lua


local skynet = require "skynet"
local max_client = 64


skynet.start(function() 
	print("server  start...")
	skynet.newservice("talkbox")
	local watchdog = skynet.newservice("watchdog")
	skynet.call(watchdog,"lua","start",{
				port = 10101,
				max_client = max_client,
		})
end)