local mtask = require "mtask"
local snax = require "snax"

mtask.start(function()
	local ps = snax.newservice ("pingserver", "hello world")
	print(ps.req.ping("foobar"))
	print(ps.post.hello())
	print(pcall(ps.req.error))
	print("Hotfix (i) :", snax.hotfix(ps, [[

local i
local hello

function accept.hello()
	i = i + 1
	print ("fix", i, hello)
end

function hotfix(...)
	local temp = i
	i = 100
	return temp
end

	]]))
	print(ps.post.hello())

	local info = mtask.call(ps.handle, "debug", "INFO")

	for name,v in pairs(info) do
		print(string.format("%s\tcount:%d time:%f", name, v.count, v.time))
	end

	print(ps.post.exit("exit")) -- == snax.kill(ps, "exit")
	mtask.exit()
end)
