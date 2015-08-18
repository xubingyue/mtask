--组播模块 Lua lib

--当只考虑一个进程时，由于同一进程共享地址空间。当发布消息时，由同一节点内的一个 multicastd 服务接收这条消息，并打包成一个 C 数据结构（包括消息指针、长度、引用计数），并把这个结构指针分别发送给同一节点的接收者。

--虽然这并没有减少消息的数量、但每个接受者只需要接收一个数据指针。当组播的消息较大时，可以节省内部的带宽。引用计数会在每个接收者收到消息后减一、最终由最后一个接收者销毁。注：如果一个订阅者在收到消息、但没有机会处理它时就退出了。则有可能引起内存泄露。少量的内存泄露影响不大，所以没有特别处理这种情况。

--当有多个节点时，每个节点内都会启动一个 multicastd 服务。它们通过 DataCenter 相互了解。multicastd 管理了本地节点创建的所有频道。在订阅阶段，如果订阅了一个远程的频道，当前节点的 multicastd 会通知远程频道的管理方，在该频道有发布消息时，把消息内容转发过来。涉及远程组播，不能直接共享指针。

--这时，multicastd 会将消息完整的发送到接收方所属节点上的同僚，由它来做节点内的组播。

local mtask = require "mtask"
local mc = require "multicast.core"

local multicastd
local multicast = {}
local dispatch = setmetatable({} , {__mode = "kv" })

local chan = {}
local chan_meta = {
	__index = chan,
	__gc = unsubscribe,
	__tostring = function (self)
		return string.format("[Multicast:%x]",self.channel)
	end,
}

local function default_conf(conf)
	conf = conf or {}
	conf.pack = conf.pack or mtask.pack
	conf.unpack = conf.unpack or mtask.unpack

	return conf
end
--你可以自由创建一个频道，并可以向其中投递任意消息。频道的订阅者可以收到投递的消息。

--你可以通过 new 函数来创建一个频道对象。你可以创建一个新频道，也可以利用已知的频道 id 绑定一个已有频道。

function multicast.new(conf) -- 创建一个频道，成功创建后，.channel 是这个频道的 id 。
	assert(multicastd, "Init first")
	local self = {}
	conf = conf or self
	self.channel = conf.channel
	if self.channel == nil then
		self.channel = mtask.call(multicastd, "lua", "NEW")
	end
	self.__pack = conf.pack or mtask.pack
	self.__unpack = conf.unpack or mtask.unpack
	self.__dispatch = conf.dispatch

	return setmetatable(self, chan_meta)
end
--当一个频道不再使用让系统回收它
function chan:delete()
	local c = assert(self.channel)
	mtask.send(multicastd, "lua", "DEL", c)
	self.channel = nil
	self.__subscribe = nil
end

--可以向一个频道发布消息。消息可以是任意数量合法的 lua 值
--光绑定到一个频道后，默认并不接收这个频道上的消息（也许你只想向这个频道发布消息）。
--你需要先调用 channel:subscribe() 订阅它。
function chan:publish(...)-- 
	local c = assert(self.channel)
	mtask.call(multicastd, "lua", "PUB", c, mc.pack(self.__pack(...)))
end
--频道订阅操作
function chan:subscribe()
	local c = assert(self.channel)
	if self.__subscribe then
		-- already subscribe
		return
	end
	mtask.call(multicastd, "lua", "SUB", c)
	self.__subscribe = true
	dispatch[c] = self
end
--频道取消订阅操作
function chan:unsubscribe()
	if not self.__subscribe then
		-- already unsubscribe
		return
	end
	local c = assert(self.channel)
	mtask.send(multicastd, "lua", "USUB", c)
	self.__subscribe = nil
end

local function dispatch_subscribe(channel, source, pack, msg, sz)
	local self = dispatch[channel]
	if not self then
		mc.close(pack)
		error ("Unknown channel " .. channel)
	end

	if self.__subscribe then
		local ok, err = pcall(self.__dispatch, self, source, self.__unpack(msg, sz))
		mc.close(pack)
		assert(ok, err)
	else
		-- maybe unsubscribe first, but the message is send out. drop the message unneed
		mc.close(pack)
	end
end

local function init()
	multicastd = mtask.uniqueservice "multicastd"
	mtask.register_protocol {
		name = "multicast",
		id = mtask.PTYPE_MULTICAST,
		unpack = mc.unpack,
		dispatch = dispatch_subscribe,
	}
end

mtask.init(init, "multicast")

return multicast