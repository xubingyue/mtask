--基于snax.gateserver实现的完整的网关服务器 作为 snax.gateserver 的使用范例
--examples/watchdog.lua 是一个可以参考的例子，它启动了一个 server/gate.lua 服务，并将处理外部连接的消息转发处理

local mtask = require "mtask"
local gateserver = require "snax.gateserver"
local netpack = require "netpack"

local watchdog
local connection = {}	-- fd -> connection : { fd , client, agent , ip, mode }
local forwarding = {}	-- agent -> connection

mtask.register_protocol {
	name = "client",
	id = mtask.PTYPE_CLIENT,
}

--这样就可以启动一个网关服务。handler 是一组自定义的消息处理函数，分别有：

local handler = {}
--open 和 close 这两个指令是保留的。它用于 gate 打开监听端口，和关闭监听端口。

--如果你希望在监听端口打开的时候，做一些初始化操作，可以提供 open 这个方法。
--source 是请求来源地址，conf 是开启 gate 服务的参数表。
function handler.open(source, conf)
	watchdog = conf.watchdog or source
end

--当一个完整的包被切分好后，message 方法被调用。这里 msg 是一个 C 指针、
--sz 是一个数字，表示包的长度（C 指针指向的内存块的长度）。
--注意：这个 C 指针需要在处理完毕后调用 C 方法 mtask_free 释放。
--(通常建议直接用封装好的库 netpack.tostring 来做这些底层的数据处理);
--或是通过 mtask.redirect 转发给别的 mtask 服务处理。

function handler.message(fd, msg, sz)
	-- recv a package, forward it
	local c = connection[fd]
	local agent = c.agent
	if agent then
		mtask.redirect(agent, c.client, "client", 0, msg, sz)
	else
		mtask.send(watchdog, "lua", "socket", "data", fd, netpack.tostring(msg, sz))
	end
end
--当一个新连接建立后，connect 方法被调用。
--传入连接的 socket fd 和新连接的 ip 地址（通常用于 log 输出）。
function handler.connect(fd, addr)
	local c = {
		fd = fd,
		ip = addr,
	}
	connection[fd] = c
	mtask.send(watchdog, "lua", "socket", "open", fd, addr)
end

local function unforward(c)
	if c.agent then
		forwarding[c.agent] = nil
		c.agent = nil
		c.client = nil
	end
end
--fd关闭操作
local function close_fd(fd)
	local c = connection[fd]
	if c then
		unforward(c)
		connection[fd] = nil
	end
end
--当一个连接断开，disconnect 被调用，fd 表示是哪个连接。
function handler.disconnect(fd)
	close_fd(fd)
	mtask.send(watchdog, "lua", "socket", "close", fd)
end
--当一个连接异常（通常意味着断开），error 被调用，除了 fd ，
--还会拿到错误信息 msg（通常用于 log 输出）。
function handler.error(fd, msg)
	close_fd(fd)
	mtask.send(watchdog, "lua", "socket", "error", fd, msg)
end

local CMD = {}

function CMD.forward(source, fd, client, address)
	local c = assert(connection[fd])
	unforward(c)
	c.client = client or 0
	c.agent = address or source
	forwarding[c.agent] = c
	gateserver.openclient(fd)
end

function CMD.accept(source, fd)
	local c = assert(connection[fd])
	unforward(c)
	gateserver.openclient(fd)
end

function CMD.kick(source, fd)
	gateserver.closeclient(fd)
end
--如果你希望让服务处理一些 mtask 内部消息，可以注册 command 方法。
--收到 lua 协议的 mtask 消息，会调用这个方法。cmd 是消息的第一个值，
--通常约定为一个字符串，指明是什么指令。source 是消息的来源地址。
--这个方法的返回值，会通过 mtask.ret/mtask.pack 返回给来源服务。
function handler.command(cmd, source, ...)
	local f = assert(CMD[cmd])
	return f(source, ...)
end

--register handlers here
gateserver.start(handler)
