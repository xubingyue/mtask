---rent_redis.lua  
local skynet = require "skynet"
local redis  = require "redis"


local conf = {
	host = "127.0.0.1" ,
	port = 6379 ,
	db = 0 ,
}

local function watching()
	local w = redis.watch(conf)
	w:subscribe "foo"
	w:psubscribe "hello.*"
	while true do 
		print("Watch",w:message())
	end
end


skynet.start(function()
	print("rent_redis call.... ...")
	skynet.fork(watching)
	local db = redis:connect(conf)
	if not db then 
		print("fail connect to redis db ...")
	else
		pritn("connect  redis  success ...")
	end
	--close connect redis 
	db:disconnect()
end)