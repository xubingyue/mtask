local mtask = require "mtask"
local cluster = require "cluster"

mtask.start(function()
	local sdb = mtask.newservice("simpledb")
	mtask.name(".simpledb", sdb)
	print(mtask.call(".simpledb", "lua", "SET", "a", "foobar"))
	print(mtask.call(".simpledb", "lua", "GET", "a"))
	cluster.open "db"
end)
