local skynet = require "skynet"
local snax = require "snax"

skynet.start(function()
	--print("self===>",skynet.self())
	skynet.error(string.format("[self ]address is[:%x]",  skynet.self()))

	local ps = snax.uniqueservice ("pingserver", "test queue")
	for i=1, 10 do
		ps.post.sleep(true,i*10)
		ps.post.hello()
	end
	for i=1, 10 do
		ps.post.sleep(false,i*10)
		ps.post.hello()
	end
 
	skynet.exit()
end)


