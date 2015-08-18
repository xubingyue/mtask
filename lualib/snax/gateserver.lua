--网关服务器 通用模板
--每个包就是 2 个字节 + 数据内容。这两个字节是 Big-Endian 编码的一个数字。
--数据内容可以是任意字节。
--所以，单个数据包最长不能超过 65535 字节。
--如果业务层需要传输更大的数据块，请在上层业务协议中解决。

local mtask = require "mtask"
local netpack = require "netpack"
local socketdriver = require "socketdriver"

local gateserver = {}

local socket	-- listen socket
local queue		-- message queue
local maxclient	-- max client
local client_number = 0
local CMD = setmetatable({}, { __gc = function() netpack.clear(queue) end })
local nodelay = false

local connection = {}

--每次收到 handler.connect 后，你都需要调用 openclient 让 fd 上的消息进入。
--默认状态下,fd仅仅是连接上你的服务器,但无法发送消息给你。
--这个步骤需要你显式的调用是因为,或许你需要在新连接建立后,把fd的控制权转交给别的服务。
--那么你可以在一切准备好以后，再放行消息。
function gateserver.openclient(fd)
	if connection[fd] then
		socketdriver.start(fd)
	end
end
--通常用于主动踢掉一个连接.
function gateserver.closeclient(fd)
	local c = connection[fd]
	if c then
		connection[fd] = false
		socketdriver.close(fd)
	end
end

function gateserver.start(handler)
	assert(handler.message)
	assert(handler.connect)

	function CMD.open( source, conf )
		assert(not socket)
		local address = conf.address or "0.0.0.0"
		local port = assert(conf.port)
		maxclient = conf.maxclient or 1024
		nodelay = conf.nodelay
		mtask.error(string.format("Listen on %s:%d", address, port))
		socket = socketdriver.listen(address, port)--这里是lua启动socket监听的入口
		socketdriver.start(socket)
		if handler.open then
			return handler.open(source, conf)
		end
	end

	function CMD.close()
		assert(socket)
		socketdriver.close(socket)
		socket = nil
	end

	local MSG = {}

	local function dispatch_msg(fd, msg, sz)
		if connection[fd] then
			handler.message(fd, msg, sz)
		else
			mtask.error(string.format("Drop message from fd (%d) : %s", fd, netpack.tostring(msg,sz)))
		end
	end

	MSG.data = dispatch_msg

	local function dispatch_queue()
		local fd, msg, sz = netpack.pop(queue)
		if fd then
			-- may dispatch even the handler.message blocked
			-- If the handler.message never block, the queue should be empty, so only fork once and then exit.
			mtask.fork(dispatch_queue)
			dispatch_msg(fd, msg, sz)

			for fd, msg, sz in netpack.pop, queue do
				dispatch_msg(fd, msg, sz)
			end
		end
	end

	MSG.more = dispatch_queue

	function MSG.open(fd, msg)
		if client_number >= maxclient then
			socketdriver.close(fd)
			return
		end
		if nodelay then
			socketdriver.nodelay(fd)
		end
		connection[fd] = true
		client_number = client_number + 1
		handler.connect(fd, msg)
	end

	local function close_fd(fd)
		local c = connection[fd]
		if c ~= nil then
			connection[fd] = nil
			client_number = client_number - 1
		end
	end

	function MSG.close(fd)
		if handler.disconnect then
			handler.disconnect(fd)
		end
		close_fd(fd)
	end

	function MSG.error(fd, msg)
		if handler.error then
			handler.error(fd, msg)
		end
		close_fd(fd)
	end

	mtask.register_protocol {
		name = "socket",
		id = mtask.PTYPE_SOCKET,	-- PTYPE_SOCKET = 6
		unpack = function ( msg, sz )
			return netpack.filter( queue, msg, sz)
		end,
		dispatch = function (_, _, q, type, ...)
			queue = q
			if type then
				MSG[type](...)
			end
		end
	}

	mtask.start(function()
		mtask.dispatch("lua", function (_, address, cmd, ...)
			local f = CMD[cmd]
			if f then
				mtask.ret(mtask.pack(f(address, ...)))
			else
				mtask.ret(mtask.pack(handler.command(cmd, address, ...)))
			end
		end)
	end)
end

return gateserver
