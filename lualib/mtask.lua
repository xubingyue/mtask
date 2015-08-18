local c = require "mtask.core"

local tostring = tostring
local tonumber = tonumber
local coroutine = coroutine
local assert = assert
local pairs = pairs
local pcall = pcall

local profile = require "profile" 

coroutine.resume = profile.resume
coroutine.yield = profile.yield

local proto = {}
local mtask = {
	PTYPE_TEXT = 0,
	PTYPE_RESPONSE = 1,
	PTYPE_MULTICAST = 2,
	PTYPE_CLIENT = 3,
	PTYPE_SYSTEM = 4,
	PTYPE_HARBOR = 5,
	PTYPE_SOCKET = 6,
	PTYPE_ERROR = 7,
	PTYPE_QUEUE = 8,	-- use in deprecated mqueue, use mtask.queue instead
	PTYPE_DEBUG = 9,
	PTYPE_LUA = 10,
	PTYPE_SNAX = 11,
}

--code cache
mtask.cache = require "mtask.codecache"

function mtask.register_protocol(class)
	local name = class.name
	local id = class.id
	assert(proto[name] == nil)
	assert(type(name)=="string" and type(id) == "numer" and id >=0 and id <= 255)
	proto[name] = class
	proto[id] = class
end

local session_id_coroutine = {}
local session_coroutine_id = {}
local session_coroutine_address = {}
local session_response = {}

local wakeup_session = {}
local sleep_session = {}

local watching_service = {}
local watching_session = {}
local dead_service = {}
local error_queue = {}
--suspend is a function 
local suspend

local function string_to_handle(str)
	return tonumber("0x"..string.sub(str,2))
end

--monitor exit
local function dispatch_error_queue()
	local session = table.remove(error_queue,1)
	if session then
		local co = session_id_coroutine[session]
		session_id_coroutine[session] =nil
		return suspend(co,coroutine.resume(co,false))
	end
end

local function _error_dispatch(error_session,error_source)
	if error_session==0 then -- service is down,don't remove from watching_service ,because user may call dead service
		if watching_service[error_source] then
			dead_service[error_source]=true
		end
		for session,src in pairs(watching_session) do
			if src==error_source then
				table.insert(error_queue,session)
			end
		end
	else  
		--capture an error for error_session
		if watching_session[error_session] then
			table.insert(error_queue,error_session)
		end
	end
end 


-------coroutine resume pool
local coroutine_pool = {}
local coroutine_yield = coroutine.yield
local coroutine_count = 0

--创建协程或者从协程池拿出一个协程
-- mtask.timeout ; mtask.fork; raw_dispatch_message 调用该API
local function co_create(f)
	local co = table.remove(coroutine_pool)
	if co==nil then
		local  print = print
		co = coroutine.create(function( ... )
			f(...)
			while true do
				f=nil
				coroutine_pool[#coroutine_pool+1] = co
				f=coroutine_yield "EXIT"

				f(coroutine_yield())
			end
		end)
		coroutine_count = coroutine_count +1
		if  coroutine_count>1024 then
			mtask.error("May overload,create 1024 task")
			coroutine_count=0
		end
	else--取到的协程不为空，使用协程池内的协程执行任务（其实是函数，这里形象点说成任务）
		coroutine.resume(co,f)	
	end
	return co
end

local function dispatch_wakeup()
	local  co = next(wakeup_session)
	if co then
		wakeup_session[co]=nil
		local  session = sleep_session[co]
		if session  then
			session_id_coroutine[session]="BREAK"--设置打断标志
			return suspend(co,coroutine.resume(co,true))
		end
	end
end

local function release_watching(address)
	local ref = watching_service[address]
	if ref then
		ref = ref -1
		if ref >0 then
			watching_service[address]=ref
		else
			watching_service[address]=nil
		end
	end
end

--suspend is local function;coroutine.resume一般是包含在suspend函数内的,因此在协程挂起后，可以做一些统筹的工作
function suspend(co,result,command,param,size)
	if not result then
		local session = session_coroutine_id[co]
		if session then
			local  addr = session_coroutine_address[co]
			if session ~= 0 then
				c.send(addr,mtask.PTYPE_ERROR,session,"")
			end
			session_coroutine_id[co]=nil
			session_coroutine_address[co]=nil
		end
		error(debug.traceback(co,tostring(command)))
	end
	--协程因mtask.call挂起，此时param为session
	if command=="CALL" then
		--保存协程（会在分发响应消息时取出协程恢复运行）
		session_id_coroutine[param]=co
	--协程因mtask.sleep或mtask.wait挂起
	elseif command=="SLEEP" then
		session_id_coroutine[param]=co
		sleep_session[co]=param
	--协程因mtask.ret挂起
	elseif command=="RETURN" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		if param==nil or session_response[co] then
			error(debug.traceback(co))
		end
		session_response[co]=true
		local ret
		if not dead_service[co_address] then
			ret = c.send(co_address,mtask.PTYPE_RESPONSE,co_session,param,size)~=nil
		elseif size==nil then--消息源挂了 并且消息大小为空
			c.trash(param,size)--回收消息的内存占用
			ret=false
		end
	--继续恢复协程执行，表面上看mtask.ret好像没有阻塞，实际上还是发生了一次阻塞动作（挂起）
		return suspend(co,coroutine.resume(co,ret))
	--协程因mtask.response挂起
	elseif command=="RESPONSE" then
		local co_session = session_coroutine_id[co]
		local co_address = session_coroutine_address[co]
		if session_response[co] then
			error(debug.traceback(co))
		end
		--该参数实际上是打包函数，默认是mtask.pack
		local  f = param 
		--定义一个闭包
		local function response(ok,...)
			if ok=="TEST" then
				if dead_service[co_address] then
					release_watching(co_address)
					f= false
					return false
				else
					return true
				end
			end

			if not f then
				if f==false then
					f=nil
					return false
				end
				--因为调用一次闭包后，f就被设置为nil了，所以不是false,就是nil了，也就是调用了多次
                error "Can't response more than once"
			end

			local ret
			if not dead_service[co_address] then
				if ok then
					ret=c.send(co_address,mtask.PTYPE_RESPONSE,co_session,f(...)) ~=nil
				else
					ret = c.send(co_address,mtask.PTYPE_ERROR,co_session,"")~=nil
				end
			else
				ret = false
			end
			release_watching(co_address)
			f=nil
			return ret
		end 

		watchdog_serice[co_address]=watching_service[co_address]+1
		session_response[co]=response -- save block func
		return suspend(co,coroutine.resume(co,response))
	--协程执行完当前任务退出，此时协程已经进入协程池
	elseif command=="EXIT" then
		--coroutine exit
		local  address = session_coroutine_address[co]
		release_watching(address)--释放引用
		session_coroutine_id[co]=nil
		session_coroutine_address[co]=nil
		session_response[co]=nil
	--协程因mtask.exit挂起
	elseif command=="QUIT" then
		--service exit
		return
	else
		error("Unknown command : " .. command .. "\n" .. debug.traceback(co))
	end
	dispatch_wakeup()	  --调度唤醒协程
	dispatch_error_queue()--调度错误队列
end

-----mtask function api
function mtask.error(...)
	local t = {...}
	for i=1,#t do
		t[i] = tostring(t[i])
	end
	return c.error(table.concat(t," "))
end
--让框架在 ti 个单位时间后，调用 func 这个函数。这不是一个阻塞 API ，当前 coroutine 会继续向下运行，而 func 将来会在新的 coroutine 中执行。
function mtask.timeout(ti,func)
	local session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local co = co_create(func)
	assert(session_id_coroutine[session]==nil)
	session_id_coroutine[session] = co
end
--将当前 coroutine 挂起 ti 个单位时间。一个单位是 1/100 秒。它是向框架注册一个定时器实现的。框架会在 ti 时间后，发送一个定时器消息来唤醒这个 coroutine 。这是一个阻塞 API 。它的返回值会告诉你是时间到了，还是被 mtask.wakeup 唤醒 （返回 "BREAK"）。
function mtask.sleep(ti)
	local  session = c.command("TIMEOUT",tostring(ti))
	assert(session)
	session = tonumber(session)
	local succ,ret  = coroutine_yield("SLEEP",session)
	sleep_session[coroutine.running()] =nil
	assert(succ,ret)

	if ret=="BREAK" then
		return "BREAK"
	end
end
--相当于 mtask.sleep(0) 。交出当前服务对 CPU 的控制权。通常在你想做大量的操作，又没有机会调用阻塞 API 时，可以选择调用 yield 让系统跑的更平滑。
function mtask.yield()
	return mtask.sleep("0")
end

function mtask.wait()
	local session = c.genid()--生成一个session
	coroutine_yield("SLEEP",session)
	local co = coroutine.running()
	sleep_session[co]=nil
	session_id_coroutine[session]=nil
end





--non block api ;coroutine 会继续向下运行，这期间服务不会重入
function mtask.send(addr,typename,...)
	local p = proto[typename]
	return c.send(addr,p.id,0,p.pack(...))--先用p.pack打包数据，然后调用c库发送消息
end

--用于退出当前的服务。mtask.exit 之后的代码都不会被运行。而且，当前服务被阻塞住的 coroutine 也会立刻中断退出。这些通常是一些 RPC 尚未收到回应。所以调用 mtask.exit() 请务必小心。
function mtask.exit()
	fork_queue = {} 
	mtask.send(".launcher","lua","REMOVE",mtask.self())

	for co,session in pairs(session_coroutine_id) do
		local address = session_coroutine_address[co]
		if session~=0 and address then
			c.redirect(address,0,mtask.PTYPE_ERROR,session,"")	
		end
	end
	-- report the sources ,I call but haven't  return
	local  tmp = {}
	for session,address in pairs(watching_session) do
		tmp[address] = true
	end
	for address in pairs(tmp) do
		c.redirect(address,0,mtask.PTYPE_ERROR,0,"")
	end
	c.command("EXIT")
	--quit service
	coroutine_yield "QUIT"
end

-- time service , mtask 的内部时钟精度为 1/100 s。
function mtask.now()
	return tonumber(c.command("NOW"))	
end
-- return  a UTC time  of mtask harobor  process  / s
function mtask.strattime()
	return tonumber(c.command("STARTTIME"))
end

function mtask.time() 
	return mtask.now()/100 + mtask.strattime()-- get now first would be better
end

--forced kill a service
function mtask.kill(name)
	if type(name) == "number" then
		mtask.send(".launcher","lua","REMOVE",name)
		name = mtask.address(name)
	end
	c.command("KILL",name)
end

function mtask.getenv(key)
	local  ret = c.command("GETENV",key)
	if ret =="" then
		return
	else
		return ret
	end
end

function mtask.setenv(key,value)
	c.command("SETENV",key.." "..value)
end

local function globalname(name,handle)
	local c = string.sub(name,1,1)
	assert(c~=':')
	if c=='.' then
		return false
	end
	assert(#name<=16)--GLOBALNAME_LENGTH is 16
	assert(tonumber(name)==nil)-- global name can't be number

	local harbor = require "mtask.harbor"
	harbor.globalname(name,handle)

	return true
end

function mtask.register(name)
	 if not globalname(name) then
	 	c.command("REG",name)
	 end
end

function mtask.name(name,handle)
	if not globalname(name,handle) then
		c.command("NAME",name..""..mtask.address(handle))
	end
end


local self_handle
function mtask.self()
	if self_handle  then
		return self_handle
	end
	self_handle = string_to_handle(c.command("REG"))
	return self_handle
end

function mtask.localname(name)
	local addr = c.command("QUERY",name)
	if addr then
		return string_to_handle(addr)
	end
end


-- lauch a C module(a so file) 
function mtask.launch(...)
	local  addr = c.command("LAUNCH",table.concat({...}," "))
	if addr then
		return string_to_handle(addr)
	end	
end

mtask.genid = assert(c.genid)--生成一个唯一 session 号。
--- 它和 mtask.send 功能类似，但更细节一些。它可以指定发送地址（把消息源伪装成另一个服务），指定发送的消息的 session 。注：address 和 source 都必须是数字地址，不可以是别名。
mtask.redirect = function(dest,source,typename,...)
	return c.redirect(dest, source, proto[typename].id, ...)
end

--以下函数使用的都是C库中的函数
mtask.pack = assert(c.pack)	
mtask.packstring = assert(c.packstring)
mtask.unpack = assert(c.unpack)--unpack 函数接收一个 lightuserdata 和一个整数 。即上面提到的 message 和 size 。lua 无法直接处理 C 指针，所以必须使用额外的 C 库导入函数来解码。mtask.tostring 就是这样的一个函数，它将这个 C 指针和长度翻译成 lua 的 string 。
mtask.tostring = assert(c.tostring)

local function yield_call(service, session)
	watching_session[session] = service --保存服务
	local succ, msg, sz = coroutine_yield("CALL", session)--挂起该协程
	watching_session[session] = nil --该协程恢复(resume)执行，去掉监视（在哪恢复的？在raw_dispatch_message的if prototype == 1分支）
	assert(succ, debug.traceback()) --如果失败了，打印堆栈
	return msg,sz --返回消息，消息大小
end

--尤其需要留意的是，mtask.call 仅仅阻塞住当前的 coroutine ，而没有阻塞整个服务。在等待回应期间，服务照样可以响应其他请求。所以，尤其要注意，在 mtask.call 之前获得的服务内的状态，到返回后，很有可能改变。
function mtask.call(addr,typename,...)
	local p = proto[typename]
	local session = c.send(addr,p.id,nil,p.pack(...))

	if session==nil then
		error("call to invalid address"..mtask.address(addr))
	end
	return p.unpack(yield_call(addr,session))
end

function mtask.rawcall(addr,typename,msg,sz)
	local p = proto[typename]
	local session = assert(c.send(addr,p.id,nil,msg,sz),"call to invalid address")
	return yield_call(addr,session)
end

--waring：mtask.ret 和 mtask.response 都是非阻塞 API 
function mtask.ret(msg,sz)
	msg = msg or ""
	return coroutine_yield("RETURN",msg,sz)
end
--获得一个闭包，以后调用这个闭包即可把回应消息发回;mtask.response 返回的闭包可用于延迟回应。调用它时，第一个参数通常是 true 表示是一个正常的回应，之后的参数是需要回应的数据。如果是 false ，则给请求者抛出一个异常。它的返回值表示回应的地址是否还有效。如果你仅仅想知道回应地址的有效性，那么可以在第一个参数传入 "TEST" 用于检测。
function mtask.response(pack)
	pack = pack or mtask.pack
	return coroutine_yield("RESPONSE",pack)
end
---- mtask.ret 在同一个消息处理的 coroutine 中只可以被调用一次，多次调用会触发异常。有时候，你需要挂起一个请求，等将来时机满足，再回应它。而回应的时候已经在别的 coroutine 中了。针对这种情况，你可以调用 mtask.response(mtask.pack) 获得一个闭包，以后调用这个闭包即可把回应消息发回。这里的参数 mtask.pack 是可选的，你可以传入其它打包函数，默认即是 mtask.pack 。
function mtask.retpack(...)
	return mtask.ret(mtask.pack(...))
end
-- wakeup a  coroutine
function mtask.wakeup(co)
	if sleep_session[co] and wakeup_session[co]==nil then
		wakeup_session[co] = true
		return true
	end
end



local function unknown_request(session, address, msg, sz, prototype)
	mtask.error(string.format("Unknown request (%s): %s", prototype, c.tostring(msg,sz)))
	error(string.format("Unknown session : %d from %x", session, address))
end

function mtask.dispatch_unknown_request(unknown)
	local prev = unknown_request
	unknown_request = unknown
	return prev
end

local function unknown_response(session, address, msg, sz)
	print("Response message :" , c.tostring(msg,sz))
	error(string.format("Unknown session : %d from %x", session, address))
end

function mtask.dispatch_unknown_response(unknown)
	local prev = unknown_response
	unknown_response = unknown
	return prev
end

local  fork_queue = {}
local  tunpack = table.unpack
--创建一个新的协程
function mtask.fork(func,...)
	local  args = {...}
	local co = co_create(function()
		func(tunpack(args))
	end)
	table.insert(fork_queue,co)--插入fork队列
end

local function raw_dispatch_message(prototype, msg, sz, session, source, ...)
	-- mtask.PTYPE_RESPONSE = 1, read mtask.h
	if prototype == 1 then -- 响应的消息（自己先发请求，等待响应，此时会挂起相应的协程）
		local co = session_id_coroutine[session] --取出session对应的协程
		if co == "BREAK" then --协程被wakeup
			session_id_coroutine[session] = nil
		elseif co == nil then --协程为空
			unknown_response(session, source, msg, sz) -- 未知的响应消息
		else
			session_id_coroutine[session] = nil --置空
			suspend(co, coroutine.resume(co, true, msg, sz)) --恢复正在等待响应的协程
		end
	else --其他服务发送来的请求
		local p = assert(proto[prototype], prototype) --取得对应的消息类别
		local f = p.dispatch --取得消息派发函数
		if f then --取得了消息派发函数
			local ref = watching_service[source] --根据消息源取得引用数
			if ref then
				watching_service[source] = ref + 1 --增加引用数
			else
				watching_service[source] = 1 --设置引用数为1
			end
			local co = co_create(f) --创建一个新的协程
			session_coroutine_id[co] = session --保存会话
			session_coroutine_address[co] = source --保存消息源
			suspend(co, coroutine.resume(co, session,source, p.unpack(msg,sz, ...)))--恢复协程
			--如果是新建的协程，则将参数通过resume直接传入主函数
			--如果是从协程池取出的，协程先resume一次获取到消息处理函数（co_create内的else分支），然后在这里再resume一次获取到具体的参数
		else --没有找到对应的消息处理函数
			unknown_request(session, source, msg, sz, proto[prototype]) --未知请求
		end
	end
end

local function dispatch_message(...) --派发消息回调
	local succ, err = pcall(raw_dispatch_message,...) --调用原始的派发消息函数
	while true do --死循环
		local key,co = next(fork_queue)--从fork队列取出一个协程
		if co == nil then--没有协程
			break--跳出循环
		end
		fork_queue[key] = nil
		local fork_succ, fork_err = pcall(suspend,co,coroutine.resume(co))
		if not fork_succ then
			if succ then
				succ = false
				err = tostring(fork_err)
			else
				err = tostring(err) .. "\n" .. tostring(fork_err)
			end
		end
	end
	assert(succ, tostring(err))
end

--用于启动一个新的 Lua 服务。name 是脚本的名字（不用写 .lua 后缀）。只有被启动的脚本的 start 函数返回后，这个 API 才会返回启动的服务的地址，这是一个阻塞 API 。如果被启动的脚本在初始化环节抛出异常，或在初始化完成前就调用 mtask.exit 退出，｀mtask.newservice` 都会抛出异常。如果被启动的脚本的 start 函数是一个永不结束的循环，那么 newservice 也会被永远阻塞住。
function mtask.newservice(name,...)
	return mtask.call(".launcher","lua","LAUNCH","snlua",name,...)
end

--和 DataCenter 不同，uniqueservice 是一个专用于服务管理的模块。它在服务地址管理上做了特别的优化。因为对于同一个名字，只允许启动一次，且不准更换。所以，在实现上，我们可以在每个节点缓存查询过的结果，而不必每次都去中心节点查询。

--mtask.uniqueservice 和 mtask.newservice 的输入参数相同，都可以以一个脚本名称找到一段 lua 脚本并启动它，返回这个服务的地址。但和 newservice 不同，每个名字的脚本在同一个 mtask 节点只会启动一次。如果已有同名服务启动或启动中，后调用的人获得的是前一次启动的服务的地址。
--它很大程度上取代了具名服务（不再推荐使用的早期特性）的功能。很多 mtask 库都附带有一个独立服务，你可以在库的初始化时，写上类似的语句：
function mtask.uniqueservice(global, ...) --启动一个唯一的LUA服务，如果global等于true,则是全局唯一的
	if global == true then
		return assert(mtask.call(".service", "lua", "GLAUNCH", ...))
	else
		return assert(mtask.call(".service", "lua", "LAUNCH", global, ...))
	end
end

function mtask.queryservice(global, ...) --查询服务
	if global == true then
		return assert(mtask.call(".service", "lua", "GQUERY", ...))
	else
		return assert(mtask.call(".service", "lua", "QUERY", global, ...))
	end
end

function mtask.address(addr)
	if type(addr) =="number" then
		return string.format("%08x",addr)
	else
		return tostring(addr)
	end
end

function mtask.harbor(addr)
	return c.harbor(addr)
end





------register protocol  lua  type msg
do
	local REG = mtask.register_protocol

	REG {
		name = "lua",
		id = mtask.PTYPE_LUA,
		pack = mtask.pack,
		unpack = mtask.unpack
	}

	REG{
		name = "response",
		id = mtask.PTYPE_RESPONSE,
	}

	REG = {
		name = "error",
		id = mtask.PTYPE_ERROR,
		unpack = function(...) return... end,
		dispatch = _error_dispatch,
	}

end

local init_func = {}
--注册初始化函数，这些函数会在调用启动函数前被调用
function mtask.init(f,name)
	assert(type(f)=="function")
	if init_func==nil then
		f()
	else
		if name==nil then
			table.insert(init_func,f)
		else
			assert(init_func[name]==nil)
			init_func[name]=f
		end
	end
end

local  function init_all()
	local  funcs = init_func
	init_func =nil
	for k,v in pairs(funcs) do
		v()--perform function
	end
end

local function init_template(start)
	init_all()
	init_func={}--重新设置初始化函数表,因为init_all()会释放init_func
	start()
	init_all()
end 

local function init_service(start)
	local ok,err = xpcall(init_template,debug.traceback,start)
	if  not ok then
		mtask.error("init service failed:"..tostring(err))
		mtask.send(".launcher","lua","ERROR")
		mtask.exit()
	else
		mtask.send(".launcher","lua","LAUNCHOK")
	end
end



function mtask.start(start_func)
	c.call_back(dispatch_message)
	mtask.timeout(0,function()
		init_service(start_func)
	end)
end
-- filter msg
function mtask.filter(f,start_func)
	c.callback(function(...)
		dispatch_message(f(...))
	end)
	mtask.timeout(0,function()
		init_service(start_func)
	end)
end
----将本服务实现为消息转发器，对一类消息进行转发。
function mtask.forward_type(map,start_func)
	c.callback(function(ptype,msg,sz,...)
		local  prototype = map[ptype]
		if prototype then
			dispatch_message(prototype,msg,sz,...)
		else
			dispatch_message(ptype,msg,sz,...)
			c.trash(msg,sz)
		end
	end,true)
	mtask.timeout(0,function() 
			init_service(start_func)
	end)
end

function mtask.endless()
	return c.command("ENDLESS")~=nil
end

--exit mtask process
function mtask.abort()
	c.command("ABORT")
end
--给当前mtask进程设置一个全局的服务监控。
function mtask.monitor(service,query)
		local monitor
	if query then
		monitor = mtask.queryservice(true, service)
	else
		monitor = mtask.uniqueservice(true, service)
	end
	assert(monitor, "Monitor launch failed")
	c.command("MONITOR", string.format(":%08x", monitor))
	return monitor
end

function mtask.mqlen()
	return tonumber(c.command "MQLEN")
end

function mtask.task(ret)
	local t = 0
		for session,co in pairs(session_id_coroutine) do
		if ret then
			ret[session] = debug.traceback(co)
		end
		t = t + 1
	end
	return t
end

function mtask.term(service)
	return _error_dispatch(0, service)
end

local function clear_pool()
	coroutine_pool = {}
end

-- Inject internal debug framework
-- 注入内部的debug框架
local debug = require "mtask.debug"
debug(mtask, {
	dispatch = dispatch_message,
	clear = clear_pool,
})

return mtask