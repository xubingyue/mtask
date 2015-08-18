-- 发起每个请求时带一个唯一 session 标识，在发送回应时，带上这个标识。

-- 这样设计可以不要求每个请求都一定要有回应，且不必遵循先提出的请求先回应的时序。

--MongoDB 的通讯协议就是这样设计的。

-- 对于第一种模式，用 mtask 的 Socket API 很容易实现，但如果在一个 coroutine 

-- 中读写一个 socket 的话，由于读的过程是阻塞的，这会导致吞吐量下降
--（前一个回应没有收到时，无法发送下一个请求）。

-- 对于第二种模式，需要用 mtask.fork 开启一个新线程来收取回应包，并自行和请求对应起来，实现比较繁琐。

-- 所以、mtask 提供了一个更高层的封装：socket channel .
local mtask = require "mtask"
local socket = require "socket"
local socketdriver = require "socketdriver"

local socket_channel = {}
local channel = {}
local channel_socket = {}
local channel_meta = {__index = channel}
local channel_socket_meta = {
	__index =channel_socket,
	__gc =function(cs)
		local fd = cs[1]
		cs[1]=false
		if fd then
			socket.shutdown(fd)
		end
	end
}

local socket_error = setmetatable({}, 
		{__tostring = function()
			 return "[Error: socket]"
			  end 
		})	-- alias for error object

socket_channel.error = socket_error

--这样就可以创建一个channel对象出来，其中 host 可以是 ip 地址或者域名，port 是端口号。
function socket_channel.channel(desc)
	local c = {
		__host = assert(desc.host),
		__port = assert(desc.port),
		__backup = desc.backup,
		__auth = desc.auth,
		__response = desc.response,	-- It's for session mode
		__request = {},	-- request seq { response func or session }	-- It's for order mode
		__thread = {}, -- coroutine seq or session->coroutine map
		__result = {}, -- response result { coroutine -> result }
		__result_data = {},
		__connecting = {},
		__sock = false,
		__closed = false,
		__authcoroutine = false,
		__nodelay = desc.nodelay,
	}

	return setmetatable(c, channel_meta)
end

local function close_channel_socket(self)
	if self.__sock then
		local so = self.__sock
		self.__sock = false
		--never raise error
		pcall(socket.close,so[1])
	end
end 

local function wakeup_all(self,errmsg)
	if self.__response then
	    for k,co in pairs(self.__thread) do
	    	self.__thread[k]=nil
	    	self.__result[co]=socket_error
	    	self.__result_data[co] =errmsg
	    	mtask.wakeup(co)
	    end
	else
		for i=1,#self.__request do
			self.__request[i]=nil
		end
		for i=1,#self._thread do
			local co = self.__thread[i]
			self.__thread[i] = nil
			self.__result[co] = socket_error
			self.__result_data[co] = errmsg
			mtask.wakeup(co)
		end
	end
end 

local function dispatch_by_session(self)
	local response = self.__response 
	--response() return session
	while self.__sock do
		local ok , session, result_ok, result_data = pcall(response, self.__sock)
		if ok and session then
			local co = self.__thread[session]
			self.__thread[session] = nil
			if co then
				self.__result[co] = result_ok
				self.__result_data[co] = result_data
				mtask.wakeup(co)
			else
				mtask.error("socket: unknown session :", session)
			end
		else
			close_channel_socket(self)
			local errormsg
			if session ~= socket_error then
				errormsg = session
			end
			wakeup_all(self, errormsg)
		end
	end
end 

local function pop_response(self)
	return table.remove(self.__request,1), table.remove(self.__thread, 1)
end 

local function push_response(self,response)
	if self.__response then
		--response is session
		self.__thread[response] =co
	else
		--response is a function,push it to __request
		table.insert(self.__request,response)
		table.insert(self.__thread,co)
	end
end 

local function dispatch_by_order(self)
	while self.__sock do
		local func,co = pop_response(self)
		if func==nil then
			if not socket.block(self.__sock[1]) then
				close_channel_socket(self)
				wakeup_all(self)
			end
		else
			local ok,result_ok,result_data = pcall(func, self.__sock)
		    if ok then
		    	self.__result[co] = result_ok
		    	self.__result_data[co]=result_data
		    	mtask.wakeup(co)
		    else
                close_channel_socket(self)
                local  errmsg 
                if result_ok~= socket_error then
                	errmsg=result_ok
                end
                self.__result[co]=socket_error
                self.__result_data[co]=errmsg
                mtask.wakeup(co)
                wakeup_all(self,errmsg)
		    end
		end
	end
end 

local function dispatch_function(self)
	if self.__response then
		return dispatch_by_session
	else
		return dispatch_by_order
	end
end

local function connect_backup(self)
	if self.__backup then
		for _,addr in ipairs(self.__backup) do
			local host,port
			if type(addr)=="table" then
				host,port=addr.host,addr.port
			else
				host=addr
				port=self.__port
			end
			mtask.error("socket:connect to backup host",host,port)
		    local fd = socket.open(host,port)
		    if fd then
		    	self.__host = host
		    	self.__port =port
		    	return fd
		    end
		end
	end
end

local function connect_once(self)
	if self.__closed then
		return false
	end
	assert(not self.__sock and not self.__authcoroutine)
	local fd = socket.open(self.__host,self.__port)
	if not fd then
		fd = connect_backup(self)
		if not fd then
			return false
		end
	end
	if self.__nodelay then
		socketdriver.nodelay(fd)
	end

	self.__sock = setmetatable({fd},channel_socket_meta)
	mtask.fork(dispatch_function(self),self)

	if self.__auth then
		self.__authcoroutine =coroutine.running()
		local ok,message = pcall(self.__auth,self)
		if not ok then
			close_channel_socket(self)
			if message ~=socket_error then
				self.__authcoroutine = false
				mtask.error("socket : auth failed",message)
			end
		end
		self.__authcoroutine =false
		if ok and not self.__sock then
			--auth may change host,so connect again
			return connect_once(self)
		end
		return ok
	end
	return true
end

local function try_connect(self,once)
	local t = 0
	while not self.__closed do
		if connect_once(self) then
			if not once(self) then
				mtask.error("socket:connect to",self.__host,self.__port)
			end
			return true
		elseif once then
			return false
		end
		if t>1000 then
			mtask.error("socket: try to reconnect",self.__host,self.__port)
	        mtask.sleep(t)
	        t=0
	    else
            mtask.sleep(t)
		end
		t=t+100
	end
end

local function check_connection(self)
	if self.__sock then
		local authco =self.__authcoroutine
		if not authco then
			return true
		end
		if authco==coroutine.running() then
			--authing
			return true
		end
	end
	if self.__closed then
		return false
	end
end

local function block_connect(self,once)
	local r=check_connection(self)
	if r~=nil then
		return r
	end

	if #self.__connecting > 0 then
		-- connecting in other coroutine
		local co = coroutine.running()
		table.insert(self.__connecting, co)
		mtask.wait()
	else
		self.__connecting[1] = true
		try_connect(self, once)
		self.__connecting[1] = nil
		for i=2, #self.__connecting do
			local co = self.__connecting[i]
			self.__connecting[i] = nil
			mtask.wakeup(co)
		end
	end

	r = check_connection(self)
	if r == nil then
		error(string.format("Connect to %s:%d failed", self.__host, self.__port))
	else
		return r
	end
end

-- socket channel 在创建时，并不会立即建立连接。
--如果你什么都不做，那么连接建立会推迟到第一次 request 请求时。
--这种被动建立连接的过程会不断的尝试，即使第一次没有连接上，也会重试。
--你也可以主动调用 channel:connect(true) 尝试连接一次。
--如果失败，抛出 error 。这里参数 true 表示只尝试一次，如果不填这个参数，则一直重试下去。
function channel:connect(once)
	if  self.__closed then
		self.__closed = false
	end
	return block_connect(self,once)
end

local function wait_for_response(self,response)
	local co = coroutine.running()
	push_response(self,response,co)
	mtask.wait()

	local result = self.__result[co]
	self.__result[co]=nil
	local result_data = self.__result_data[co]
	self.__result_data[co] = nil

	if result == socket_error then
		error(socket_error)
	else
		assert(result, result_data)
		return result_data
	end 
end
--这里，req 是一个字符串，即请求包。response 是一个 function ，
--用来收取回应包。返回值是一个字符串，是由 response 函数返回的回应包的内容
--（可以是任意类型）。response 函数需要定义成这个样子：function response(sock) return true, sock:readline() end
--sock 是由 request 方法传入的一个对象，sock 有两个方法：read(self, sz) 和 readline(self, sep) 。read 可以读指定字节数；readline 可以读以 sep 分割（默认为 \n）的一个字符串（不包含分割符）。

--response 函数的第一个返回值需要是一个 boolean ，如果为 true 表示协议解析正常；如果为 false 表示协议出错，这会导致连接断开且让 request 的调用者也获得一个 error 。

--在 response 函数内的任何异常以及 sock:read 或 sock:readline 读取出错，都会以 error 的形式抛给 request 的调用者。
function channel:request(request, response)
	assert(block_connect(self, true))	-- connect once

	if not socket.write(self.__sock[1], request) then
		close_channel_socket(self)
		wakeup_all(self)
		error(socket_error)
	end

	if response == nil then
		-- no response
		return
	end

	return wait_for_response(self, response)
end
--channel:response 则可以用来单向接收一个包。
function channel:response(response)
	assert(block_connect(self))

	return wait_for_response(self, response)
end
-- 可以关闭一个 channel ，通常你可以不必主动关闭它，gc 会回收 channel 占用的资源。
function channel:close()
	if not self.__closed then
		self.__closed = true
		close_channel_socket(self)
	end
end

function channel:changehost(host, port)
	self.__host = host
	if port then
		self.__port = port
	end
	if not self.__closed then
		close_channel_socket(self)
	end
end

function channel:changebackup(backup)
	self.__backup = backup
end

channel_meta.__gc = channel.close

local function wrapper_socket_function(f)
	return function(self, ...)
		local result = f(self[1], ...)
		if not result then
			error(socket_error)
		else
			return result
		end
	end
end

channel_socket.read = wrapper_socket_function(socket.read)
channel_socket.readline = wrapper_socket_function(socket.readline)

return socket_channel
