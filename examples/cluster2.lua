local mtask = require "mtask"
local cluster = require "cluster"

mtask.start(function()
	-- query name "sdb" of cluster db.
	local sdb = cluster.query("db", "sdb")
	print("db.sbd=",sdb)
	local proxy = cluster.proxy("db", sdb)
	local largekey = string.rep("X", 128*1024)
	local largevalue = string.rep("R", 100 * 1024)
	print(mtask.call(proxy, "lua", "SET", largekey, largevalue))
	local v = mtask.call(proxy, "lua", "GET", largekey)
	assert(largevalue == v)

	print(cluster.call("db", sdb, "GET", "a"))
	print(cluster.call("db2", sdb, "GET", "b"))

	-- test snax service
	local pingserver = cluster.snax("db", "pingserver")
	print(pingserver.req.ping "hello")
end)
