--snax 是一个方便 mtask 服务实现的简单框架。（简单是相对于 mtask 的 api 而言）

--使用 snax 服务先要在 Config 中配置 snax 用于路径查找。每个 snax 服务都有一个用于启动服务的名字，推荐按 lua 的模块命名规则，但目前不推荐在服务名中包含"点" （在路径搜索上尚未支持 . 与 / 的替换）。在启动服务时会按查找路径搜索对应的文件。

--snax 服务用 lua 编写，但并不是一个独立的 lua 程序。它里面包含了一组 lua 函数，会被 snax 框架分析加载。

--test/pingserver.lua 就是一个简单的 snax 服务范例：

--注：snax 的接口约定是通过分析对应的 lua 源文件完成的。所以无论是消息发送方还是消息接收方都必须可以读到其消息处理的源文件。不仅 snax.newservice 会加载对应的源文件生成新的服务；调用者本身也会在自身的 lua 虚拟机内加载对应的 lua 文件做一次分析；同样， snax.bind 也会按 typename 分析对应的源文件。

local mtask = require "mtask"
local snax_interface = require "snax.interface"

local snax = {}
local typeclass = {}

local G = { require = function() end }

mtask.register_protocol {
	name = "snax",
	id = mtask.PTYPE_SNAX,
	pack = mtask.pack,
	unpack = mtask.unpack,
}


function snax.interface(name)
	if typeclass[name] then
		return typeclass[name]
	end

	local si = snax_interface(name, G)

	local ret = {
		accept = {},
		response = {},
		system = {},
	}

	for _,v in ipairs(si) do
		local id, group, name, f = table.unpack(v)
		ret[group][name] = id
	end

	typeclass[name] = ret
	return ret
end

local meta = { __tostring = function(v) return string.format("[%s:%x]", v.type, v.handle) end}

local mtask_send = mtask.send
local mtask_call = mtask.call

local function gen_post(type, handle)
	return setmetatable({} , {
		__index = function( t, k )
			local id = assert(type.accept[k] , string.format("post %s no exist", k))
			return function(...)
				mtask_send(handle, "snax", id, ...)
			end
		end })
end

local function gen_req(type, handle)
	return setmetatable({} , {
		__index = function( t, k )
			local id = assert(type.response[k] , string.format("request %s no exist", k))
			return function(...)
				return mtask_call(handle, "snax", id, ...)
			end
		end })
end

local function wrapper(handle, name, type)
	return setmetatable ({
		post = gen_post(type, handle),
		req = gen_req(type, handle),
		type = name,
		handle = handle,
		}, meta)
end

local handle_cache = setmetatable( {} , { __mode = "kv" } )

function snax.rawnewservice(name, ...)
	local t = snax.interface(name)
	local handle = mtask.newservice("snaxd", name)
	assert(handle_cache[handle] == nil)
	if t.system.init then
		mtask.call(handle, "snax", t.system.init, ...)
	end
	return handle
end
--也会按 typename 分析对应的LUA源文件
function snax.bind(handle, type)
	local ret = handle_cache[handle]
	if ret then
		assert(ret.type == type)
		return ret
	end
	local t = snax.interface(type)
	ret = wrapper(handle, type, t)
	handle_cache[handle] = ret
	return ret
end
--可以把一个服务启动多份.传入服务名和参数,它会返回一个对象,用于和这个启动的服务交互。
--如果多次调用newservice,即使名字相同,也会生成多份服务的实例,它们各自独立,由不同的对象区分。
--这种方式可以看成是启动了一个匿名服务，启动后只能用地址（以及对服务地址的对象封装）与之通讯
function snax.newservice(name, ...)
	local handle = snax.rawnewservice(name, ...)
	return snax.bind(handle, name)
end

local function service_name(global, name, ...)
	if global == true then
		return name
	else
		return global
	end
end
--和api snax.newservice()类似,但在一个节点上只会启动一份同名服务。如果你多次调用它，会返回相同的对象。
--具名服务，之后可以用名字找到它;对具名服务惰性初始化
function snax.uniqueservice(name, ...)
	local handle = assert(mtask.call(".service", "lua", "LAUNCH", "snaxd", name, ...))
	return snax.bind(handle, name)
end
--和上面的 api 类似,但在整个 mtask 网络中（如果你启动了多个节点），只会有一个同名服务。
--具名服务，之后可以用名字找到它;对具名服务惰性初始化
--如果你在代码中写了多处服务启动，第一次会生效，后续只是对前面启动的服务的查询。往往我们希望明确服务的启动流程（在启动脚本里就把它们启动好）；尤其是全局（整个 mtask 网络可见）的服务，我们还希望明确它启动在哪个结点上（如果是惰性启动，你可能无法预知哪个节点先把这个服务启动起来的）。这时，可以使用下面两个 api ：
function snax.globalservice(name, ...)
	local handle = assert(mtask.call(".service", "lua", "GLAUNCH", "snaxd", name, ...))
	return snax.bind(handle, name)
end
--根据name 查询服务
--snax.queryservice(name) ：查询当前节点的具名服务，返回一个服务对象。如果服务尚未启动，那么一直阻塞等待它启动完毕
function snax.queryservice(name)
	local handle = assert(mtask.call(".service", "lua", "QUERY", "snaxd", name))
	return snax.bind(handle, name)
end
--查询一个全局名字的服务，返回一个服务对象。如果服务尚未启动，那么一直阻塞等待它启动完毕。
function snax.queryglobal(name)
	local handle = assert(mtask.call(".service", "lua", "GQUERY", "snaxd", name))
	return snax.bind(handle, name)
end
--对于匿名服务，你无法在别处通过名字得到和它交互的对象。如果你有这个需求，可以把对象的 .handle 域通过消息发送给别人。 handle 是一个数字，即 snax 服务的 mtask 服务地址。
--这个数字的接收方可以通过 snax.bind(handle, typename) 把它转换成服务对象。这里第二个参数需要传入服务的启动名，以用来了解这个服务有哪些远程方法可以供调用。当然，你也可以直接把 .type 域和 .handle 一起发送过去，而不必在源代码上约定。


--snax 服务退出
function snax.kill(obj, ...)
	local t = snax.interface(obj.type)
	mtask_call(obj.handle, "snax", t.system.exit, ...)
end
--用来获取自己这个服务对象，它等价于 snax.bind(mtask.self(), SERVER_NAME)
function snax.self()
	return snax.bind(mtask.self(), SERVICE_NAME)
end
--退出当前服务，它等价于 snax.kill(snax.self(), ...) 。
function snax.exit(...)
	snax.kill(snax.self(), ...)
end

local function test_result(ok, ...)
	if ok then
		return ...
	else
		error(...)
	end
end

function snax.hotfix(obj, source, ...)
	local t = snax.interface(obj.type)
	return test_result(mtask_call(obj.handle, "snax", t.system.hotfix, source, ...))
end

return snax
