local mtasklocal mtask = require "mtask"
local sc = require "socketchannel"
local socket = require "socket"
local cluster = require "cluster.core"

local config_name = mtask.getenv "cluster"
local node_address = {}

local function loadconfig()
	local f = assert(io.open(config_name))
	local source =f:read "*a"
	f:close()
	assert(load(source,"@"..config_name,"t",node_address))()
end

local node_session={}
local command = {}

local function read_response(sock)
	local sz = socket.header(sock:read(2))
	local msg = sock:read(sz)
	return cluster.unpackresponse(msg)-- session, ok, data
end

local function open_channel(t, key)
	local host, port = string.match(node_address[key], "([^:]+):(.*)$")
	local c = sc.channel {
		host = host,
		port = tonumber(port),
		response = read_response,
	}
	assert(c:connect(true))
	t[key] = c
	node_session[key] = 1
	return c
end

local node_channel = setmetatable({}, { __index = open_channel })

function command.reload()
	loadconfig()
	mtask.ret(mtask.pack(nil))
end

function command.listen(source, addr, port)
	local gate = mtask.newservice("gate")
	if port == nil then
		addr, port = string.match(node_address[addr], "([^:]+):(.*)$")
	end
	mtask.call(gate, "lua", "open", { address = addr, port = port })
	mtask.ret(mtask.pack(nil))
end

local function send_request(source, node, addr, msg, sz)
	local request
	local c = node_channel[node]
	local session = node_session[node]
	-- msg is a local pointer, cluster.packrequest will free it
	request, node_session[node] = cluster.packrequest(addr, session , msg, sz)

	return c:request(request, session)
end

function command.req(...)
	local ok, msg, sz = pcall(send_request, ...)
	if ok then
		mtask.ret(msg, sz)
	else
		mtask.error(msg)
		mtask.response()(false)
	end
end

local proxy = {}

function command.proxy(source, node, name)
	local fullname = node .. "." .. name
	if proxy[fullname] == nil then
		proxy[fullname] = mtask.newservice("clusterproxy", node, name)
	end
	mtask.ret(mtask.pack(proxy[fullname]))
end

local request_fd = {}

function command.socket(source, subcmd, fd, msg)
	if subcmd == "data" then
		local addr, session, msg = cluster.unpackrequest(msg)
		local ok , msg, sz = pcall(mtask.rawcall, addr, "lua", msg)
		local response
		if ok then
			response = cluster.packresponse(session, true, msg, sz)
		else
			response = cluster.packresponse(session, false, msg)
		end
		socket.write(fd, response)
	elseif subcmd == "open" then
		mtask.error(string.format("socket accept from %s", msg))
		mtask.call(source, "lua", "accept", fd)
	else
		mtask.error(string.format("socket %s %d : %s", subcmd, fd, msg))
	end
end

mtask.start(function()
	loadconfig()
	mtask.dispatch("lua",function(session,source,cmd,... )
		local f= assert(command[cmd])
		f(source,...)
	end)
end)
