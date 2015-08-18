local  driver = require "socketdriver"
local mtask = require "mtask"
local assert = assert

local socket = {} --api
local buffer_pool = {} --strore all msg buffer obj
local socket_pool = setmetatable(
	{},
	{ __gc = function(p)
		for id,v in pairs(p) do
            driver.close(id)
            p[id] =nil --don't need clear v.buffer,beacuse buffer pool will be free at the end
		end
	end
	}
)

local socket_message = {}

local  function wakeup(s)
	local co = s.co
	if co then
		s.co = nil
		mtask.wakeup(co)
	end
end 
-- wakeup closing coroutine every time suspend,
-- beacause socket.close() will wait last socket buffer operation before clear the buffer.
local function suspend(s)
	assert(not s.co)
	s.co = coroutine.running()
	mtask.wait()
	if s.closing then
		mtask.wakeup(s.closing)
	end
end


-- read mtask_socket.h for these macro
socket_message[1] = function(id,size,data)
	local s = socket_pool[id]
	if  s==nil then
		mtask.error("socket: drop package from"..id)
		driver.drop(data,size)
		return
	end

	local sz = driver.push(s.buffer,buffer_pool,data,size)
	local rr = s.read_required
	local rrt = type(rr)
	if  rrt=="number" then
		--read size
		if sz>=rr then
			s.read_required=nil
			wakeup(s)
		end
	else
		if s.buffer_limit and sz >s.buffer_limit then
			mtask.error(string.format("socket buffer overflow: fd=%d size=%d", id , sz))
			driver.clear(s.buffer_pool,buffer_pool)
			driver.close(id)
			return
		end
		if  rrt=="string" then
			if driver.readline(s.buffer,nil,rr) then
				s.read_required=nil
				wakeup(s)
			end
		end
	end
end
-- mtask_SOCKET_TYPE_CONNECT = 2
socket_message[2] = function(id,_,addr)
	local  s= socket_pool[id]
	if s==nil then
		return
	end
	--log remote addr
	s.connected = true
	wakeup(s)
end
-- mtask_SOCKET_TYPE_CLOSE = 3
socket_message[3] =function(id)
	local  s= socket_pool[id]
	if s==nil then
		return
	end
	s.connected = false
	wakeup(s)
end
-- mtask_SOCKET_TYPE_ACCEPT = 4
socket_message[4] =function(id)
	local  s= socket_pool[id]
	if s==nil then
		return
	end
	s.callback(newid,addr)
end


socket_message[5] = function(id)
	local  s= socket_pool[id]
	if s==nil then
		mtask.error("socket: error on unknown",id)
		return
	end
	if  s.connected then
		mtask.error("socket:error on",id)
	end
	s.connected = false

	wakeup(s)
end

socket_message[6] =function(id,size,data,address)
	local s = socket_pool[id]
	if s==nil or s.callback==nil then
		mtask.error("socket:drop udp package from",id)
		driver.drop(data,size)
		return
	end
	s.callback(data,size,address)
end

mtask.register_protocol {
	name = "socket",
	id = mtask.PTYPE_SOCKET, --6
	unpack = driver.unpack,
	dispatch = function(_,_,t,...)
		socket_message[t](...)
	end
}

local function connect(id,func)
	local newbuffer
	if func ==nil then
		newbuffer=driver.buffer()
	end

	local s = {
		id=id,
		buffer=newbuffer,
		connected=false,
		read_required =false,
		co=false,
		callback=func,
		protocol = "TCP",
	}

	socket_pool[id] =s
	suspend(s)

	if s.connected then
		return id
	else
		socket_pool[id]=nil
	end
end 

function socket.open(addr,port)
	local  id = driver.connect(addr,port)
	return connect(id)
end

function socket.bind(os_fd)
	local id = driver.bind(os_fd)
	return connect(id)
end

function socket.stdin()
	return socket.bind(0)
end
	
function socket.start(id,func)
	driver.start(id)
	return connect(id,func)
end
--强行关闭一个连接。和 close 不同的是，它不会等待可能存在的其它 coroutine 的读操作。
--一般不建议使用这个 API ，但如果你需要在 __gc 元方法中关闭连接的话，shutdown 是一个比 close 更好的选择（因为在 gc 过程中无法切换 coroutine）。
function socket.shutdown(id)
	local  s = socket_pool[id]
	if s then
		if s.buffer then
			driver.clear(s.buffer,buffer_pool)
		end
		if s.connected then
			driver.close(id)
		end
	end
end
--关闭一个连接，这个 API 有可能阻塞住执行流。
--因为如果有其它 coroutine 正在阻塞读这个 id 对应的连接，会先驱使读操作结束，close 操作才返回。
function socket.close(id)
	local  s = socket_pool[id]
	if s==nil then
		return
	end
	if s.connected then
		driver.close(s.id)
		--notice: call socket.close in __gc should be carefully,
		--because mtask.wait never return in __gc,so driver.clear may be not called
		if s.co then
			--reading this socket on anther coroutine 
			--so don't shutdown (clear the bufer) directly
			--wait reading coroutine read the buffer
			assert(not s.closing)
			s.closing = coroutine.running()
			mtask.wait()
		else
			suspend(s)
		end
		s.connected =false
	end
	socket.shutdown(id)
	assert(s.lock_set==nil or next(s.lock_set)==nil)
	socket_pool[id] = nil
end

--从一个 socket 上读 sz 指定的字节数。
--如果读到了指定长度的字符串，它把这个字符串返回。
--如果连接断开导致字节数不够，将返回一个 false 加上读到的字符串。
--如果 sz 为 nil ，则返回尽可能多的字节数，但至少读一个字节（若无新数据，会阻塞）。
function socket.read(id,sz)
	local s = socket_pool[id]
	assert(s)
	if sz==nil then
		--read some bytes
		local ret = driver.readall(s.buffer,buffer_pool)
		if ret~="" then
			return ret
		end

		if not s.connected then
			return false,ret
		end

		assert(not s.read_required)
		s.read_required =0
		suspend(s)
		ret = driver.readall(s.buffer,buffer_pool)
		if ret~="" then
			return ret
		else
			return false,ret
		end
	end

	local ret = driver.pop(s.buffer,buffer_pool,sz)
	if ret then
		return ret
	end

	if not s.connected then
		return false,driver.readall(s.buffer,buffer_pool)
	end

	assert(not s.read_required)
	s.read_required = sz
	suspend(s)
	ret = driver.pop(s.buffer,buffer,sz)
	if ret then
		return ret
	else
		return false,driver.readall(s.buffer,buffer_pool)
	end
end

-- 从一个 socket 上读所有的数据，直到 socket 主动断开，
-- 或在其它 coroutine 用 socket.close 关闭它
function socket.readall(id)
	local s = socket_pool[id]
	assert(s)
	if not s.connected then
		local r = driver.readall(s.buffer,buffer_pool)
		return r~="" and r
	end
	assert(not s.read_required)
	s.read_required =true
	suspend(s)
	assert(s.connected ==false)
	return driver.readall(s.buffer,buffer_pool)
end
 --从一个 socket 上读一行数据。sep 指行分割符。
 --默认的 sep 为 "\n"。
 --读到的字符串是不包含这个分割符的
function socket.readline(id,sep)
	sep = sep or "\n"
	local s = socket_pool[id]
	assert(s)
	local ret = driver.readline(s.buffer,buffer_pool)
	if ret then
		return ret
	end

	if not s.connected then
		return false,driver.readall(s.buffer,buffer_pool)
	end
	assert(not s.read_required)
	s.read_required = sep
	suspend(s)
	if s.connected then
		return driver.readline(s.buffer, buffer_pool, sep)
	else
		return false, driver.readall(s.buffer, buffer_pool)
	end
end

--等待一个 socket 可读。 block
function socket.block(id)
	local s = socket_pool[id]
	if not s or not s.connected then
		return false
	end

	assert(not s.read_required)
	s.read_required =0
	suspend(s)
	return s.connected
end


socket.write = assert(driver.send)

socket.lwrite = assert(driver.lsend)

socket.header = assert(driver.header)

function socket.invalid(id)
	return socket_pool[id]==nil
end
--监听一个端口，返回一个 id ，供 start 使用。
function socket.listen(host,port,backlog)
	if port ==nil then
		host,port=string.match(host,"([^:]+):(.+)$")
		port = tonumber(port)
	end
	return driver.listen(host,port,backlog)
end

function socket.lock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = s.lock
	if not lock_set then
		lock_set = {}
		s.lock = lock_set
	end
	if #lock_set == 0 then
		lock_set[1] = true
	else
		local co = coroutine.running()
		table.insert(lock_set, co)
		mtask.wait()
	end
end

function socket.unlock(id)
	local s = socket_pool[id]
	assert(s)
	local lock_set = assert(s.lock)
	table.remove(lock_set,1)
	local co = lock_set[1]
	if co then
		mtask.wakeup(co)
	end
end
--清除 socket id 在本服务内的数据结构，但并不关闭这个 socket 。
--这可以用于你把 id 发送给其它服务，以转交 socket 的控制权。
function socket.abandon( ... )
	local s = socket_pool[id]
	if s and s.buffer then
		driver.clear(s.buffer,buffer_pool)
	end
	socket_pool[id] = nil
end

function socket.limit(id,limit)
	local  s = assert(socket_pool[id])
	s.buffer_limit = limit
end


----------------UDP-----------------

local  udp_socket = {}

local function create_udp_object(id,cb)
	socket_pool[id] = {
		id=id,
		connected = true,
		protocol ="UDP",
		callback=cb,
	}
end

function socket.udp(callback,host,port)
	local id = driver.udp(host,port)
	create_udp_object(id,callback)
	return id
end

function socket.udp_connect(id,addr,port,callback)
	local  obj = socket_pool[id]
	if obj then
		assert(obj.protocol=="UDP")
		if callback then
			obj.callback=callback
		end
	else
		create_udp_object(id,callback)
	end
	driver.udp_connect(id,addr,port)
end
--向一个网络地址发送一个数据包。
socket.sendto=assert(driver.udp_send)

socket.udp_address =assert(driver.udp_address)


return socket
