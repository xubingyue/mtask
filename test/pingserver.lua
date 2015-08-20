--RPC远程调用
local skynet = require "skynet"
local queue = require "skynet.queue"
local snax = require "snax"

local i = 0
local hello = "hello"
--要定义这类远程方法，可以通过定义 function response.foobar(...)来声明一个远程方法。
--foobar 是方法名,response 前缀表示这个方法一定有一个回应。你可以通过函数返回值来回应远程调用。

function response.ping(hello)
	skynet.sleep(100)
	return hello
end

-- response.sleep and accept.hello share one lock
local lock

function accept.sleep(queue, n)
	if queue then
		lock(
		function()
			print("accept.sleep－－－》queue=",queue, n)
			skynet.sleep(n)
		end)
	else
		print("accept.sleep－－>queue=",queue, n)
		skynet.sleep(n)
	end
end

function accept.hello()
	print("accept.hello called....")
	lock(function()
	i = i + 1
	print ("accept.hello===>",i, hello)
	end)
end

function accept.exit(...)
	print("snax.exit",...)
	snax.exit(...)
end

function response.error()
	error "throw an error"
end

function init( ... )
	print ("ping server start:", ...)
	-- init queue
	lock = queue()
end

function exit(...)
	print ("ping server exit:", ...)
end
