local mtask = require "mtask"
local snax = require "snax"

mtask.start(function()
	--print("self===>",mtask.self())
	mtask.error(string.format("[self ]address is[:%x]",  mtask.self()))

	local ps = snax.uniqueservice ("pingserver", "test queue")
	for i=1, 10 do
		ps.post.sleep(true,i*10)
		ps.post.hello()
	end
	for i=1, 10 do
		ps.post.sleep(false,i*10)
		ps.post.hello()
	end
 
	mtask.exit()
end)


