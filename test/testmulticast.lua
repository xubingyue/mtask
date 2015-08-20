local mtask = require "mtask"
local mc = require "multicast"
local dc = require "datacenter"

local mode = ...
print("mode==>",mode)
if mode == "sub" then

mtask.start(function()
	print("111111")
	mtask.dispatch("lua", function (_,_, cmd, channel)
		assert(cmd == "init")
		local c = mc.new {
			channel = channel ,
			dispatch = function (channel, source, ...)
				print(string.format("%s <=== %s %s",mtask.address(mtask.self()),mtask.address(source), channel), ...)
			end
		}
		print(mtask.address(mtask.self()), "sub", c)
		c:subscribe()
		mtask.ret(mtask.pack())
	end)
end)

else

mtask.start(function()
	print("2222")
	local channel = mc.new() -- 创建一个频道，成功创建后，.channel 是这个频道的 id 。
	print("New channel", channel)
	for i=1,10 do
		local sub = mtask.newservice(SERVICE_NAME, "sub")
		mtask.call(sub, "lua", "init", channel.channel)
	end

	dc.set("MCCHANNEL", channel.channel)	-- for multi node test

	print(mtask.address(mtask.self()), "===>", channel)
	channel:publish("Hello World")
end)

end