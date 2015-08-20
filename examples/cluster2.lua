local mtask = require "mtask"
local cluster = require "cluster"

mtask.start(function()
	local proxy = cluster.proxy("db", ".simpledb")
	print(mtask.call(proxy, "lua", "GET", "a"))
	print(cluster.call("db", ".simpledb", "GET", "a"))
end)
