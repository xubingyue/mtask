local core = require "sproto.core"
local assert =assert

local sproto = {}
local host= {}

local weak_mt ={__mode="kv"}
local sproto_mt = {__index=sproto}
local host_mt ={__index=host}

function sproto_mt:gc()
	core.deleteproto(self.__cobj)
end

function sproto.new(pbin)
	local cobj = assert(core.newproto(pbin))
	local self ={
	     __cobj = cobj,
	     __tcache = setmetatable({},weak_mt),
	     __pcache = setmetatable({},weak_mt),
	}
	return setmetatable(self,sproto_mt)
end

function sproto.parse(ptext)
	local parser = require "sprotoparser"
	local pbin = parser.parse(ptext)
	return sproto.new(pbin)
end

--这条调用会返回一个 host 对象，用于处理接收的消息。
function sproto:host(packagename)
	packagename = packagename or "package"
	local obj = {
		__proto = self,
		__package = core.querytype(self.__cobj,packagename),
		__session = {},
	}
	return setmetatable(obj,host_mt)
end

local function querytype(self, typename)
	local v = self.__tcache[typename]
	if not v then
		v = core.querytype(self.__cobj, typename)
		self.__tcache[typename] = v
	end

	return v
end

function sproto:encode(typename, tbl)
	local st = querytype(self, typename)
	return core.encode(st, tbl)
end

function sproto:decode(typename, bin)
	local st = querytype(self, typename)
	return core.decode(st, bin)
end

function sproto:pencode(typename, tbl)
	local st = querytype(self, typename)
	return core.pack(core.encode(st, tbl))
end

function sproto:pdecode(typename, bin)
	local st = querytype(self, typename)
	return core.decode(st, core.unpack(bin))
end

local function queryproto(self, pname)
	local v = self.__pcache[pname]
	if not v then
		local tag, req, resp = core.protocol(self.__cobj, pname)
		assert(tag, pname .. " not found")
		if tonumber(pname) then
			pname, tag = tag, pname
		end
		v = {
			request = req,
			response =resp,
			name = pname,
			tag = tag,
		}
		self.__pcache[pname] = v
		self.__pcache[tag]  = v
	end

	return v
end

local header_tmp = {}

local function gen_response(self, response, session)
	return function(args)
		header_tmp.type = nil
		header_tmp.session = session
		local header = core.encode(self.__package, header_tmp)
		if response then
			local content = core.encode(response, args)
			return core.pack(header .. content)
		else
			return core.pack(header)
		end
	end
end

--host:dispatch(msgcontent)
--用于处理一条消息.这里的msgcontent也是一个字符串,或是一个userdata(指针)加一个长度。
--它应符合上述的以 sproto 的 0-pack 方式打包的包格式。

-- dispatch 调用有两种可能的返回类别，由第一个返回值决定：
-- REQUEST:第一个返回值为 "REQUEST" 时，表示这是一个远程请求。
-- 			如果请求包中没有 session 字段，表示该请求不需要回应。
-- 			这时，第 2 和第 3 个返回值分别为消息类型名（即在sproto定义中提到的某个以 .
-- 		    开头的类型名），以及消息内容（通常是一个 table);
-- 		    如果请求包中有session 字段,那么还会有第 4 个返回值：一个用于生成回应包的函数。

-- RESPONSE:第一个返回值为"RESPONSE"时,第2和第3个返回值分别为session
-- 和消息内容.消息内容通常是一个table,但也可能不存在内容（仅仅是一个回应确认)。

function host:dispatch(...)
	local bin = core.unpack(...)
	header_tmp.type = nil
	header_tmp.session = nil
	local header, size = core.decode(self.__package, bin, header_tmp)
	local content = bin:sub(size + 1)
	if header.type then
		-- request
		local proto = queryproto(self.__proto, header.type)
		local result
		if proto.request then
			result = core.decode(proto.request, content)
		end
		if header_tmp.session then
			return "REQUEST", proto.name, result, gen_response(self, proto.response, header_tmp.session)
		else
			return "REQUEST", proto.name, result
		end
	else
		-- response
		local session = assert(header_tmp.session, "session not found")
		local response = assert(self.__session[session], "Unknown session")
		self.__session[session] = nil
		if response == true then
			return "RESPONSE", session
		else
			local result = core.decode(response, content)
			return "RESPONSE", session, result
		end
	end
end
--attach可以构造一个发送函数,用来将对外请求打包编码成可以被dispatch正确解码的数据包。
--这个sender函数接受三个参数(name,args,session)name 是消息的字符串名、args 是一张保存用消息内容的 table ，而 session 是你提供的唯一识别号，用于让对方正确的回应。
--当你的协议不规定不需要回应时，session 可以不给出。同样，args 也可以为空。
function host:attach(sp)-- 这里的 sp 指向外发出的消息协议定义
	return function(name, args, session)
		local proto = queryproto(sp, name)
		header_tmp.type = proto.tag
		header_tmp.session = session
		local header = core.encode(self.__package, header_tmp)

		if session then
			self.__session[session] = proto.response or true
		end

		if args then
			local content = core.encode(proto.request, args)
			return core.pack(header ..  content)
		else
			return core.pack(header)
		end
	end
end

return sproto




