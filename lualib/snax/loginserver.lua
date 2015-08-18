--一个通用的登陆服务器模版 
local mtask = require "mtask"
local socket = require "socket"
local crypt = require "crypt"
local table = table
local string = string
local assert = assert

--[[

Protocol:

	line (\n) based text protocol

	1. Server->Client : base64(8bytes random challenge)
	2. Client->Server : base64(8bytes handshake client key)
	3. Server: Gen a 8bytes handshake server key
	4. Server->Client : base64(DH-Exchange(server key))
	5. Server/Client secret := DH-Secret(client key/server key)
	6. Client->Server : base64(HMAC(challenge, secret))
	7. Client->Server : DES(secret, base64(token))
	8. Server : call auth_handler(token) -> server, uid (A user defined method)
	9. Server : call login_handler(server, uid, secret) ->subid (A user defined method)
	10. Server->Client : 200 base64(subid)

Error Code:
	400 Bad Request . challenge failed
	401 Unauthorized . unauthorized by auth_handler
	403 Forbidden . login_handler failed
	406 Not Acceptable . already in login (disallow multi login)

Success:
	200 base64(subid)
]]

local socket_error = {}
local function assert_socket(v, fd)
	if v then
		return v
	else
		mtask.error(string.format("auth failed: socket (fd = %d) closed", fd))
		error(socket_error)
	end
end

local function write(fd, text)
	assert_socket(socket.write(fd, text), fd)
end

local function launch_slave(auth_handler)
	local function auth(fd, addr)
		fd = assert(tonumber(fd))
		mtask.error(string.format("connect from %s (fd = %d)", addr, fd))
		socket.start(fd)

		-- set socket buffer limit (8K)
		-- If the attacker send large package, close the socket
		socket.limit(fd, 8192)

		local challenge = crypt.randomkey()
		write(fd, crypt.base64encode(challenge).."\n")

		local handshake = assert_socket(socket.readline(fd), fd)
		local clientkey = crypt.base64decode(handshake)
		if #clientkey ~= 8 then
			error "Invalid client key"
		end
		local serverkey = crypt.randomkey()
		write(fd, crypt.base64encode(crypt.dhexchange(serverkey)).."\n")

		local secret = crypt.dhsecret(clientkey, serverkey)

		local response = assert_socket(socket.readline(fd), fd)
		local hmac = crypt.hmac64(challenge, secret)

		if hmac ~= crypt.base64decode(response) then
			write(fd, "400 Bad Request\n")
			error "challenge failed"
		end

		local etoken = assert_socket(socket.readline(fd),fd)

		local token = crypt.desdecode(secret, crypt.base64decode(etoken))

		local ok, server, uid =  pcall(auth_handler,token)

		socket.abandon(fd)
		return ok, server, uid, secret
	end

	local function ret_pack(ok, err, ...)
		if ok then
			mtask.ret(mtask.pack(err, ...))
		else
			error(err)
		end
	end

	mtask.dispatch("lua", function(_,_,...)
		ret_pack(pcall(auth, ...))
	end)
end

local user_login = {}

local function accept(conf, s, fd, addr)
	-- call slave auth
	local ok, server, uid, secret = mtask.call(s, "lua",  fd, addr)
	socket.start(fd)

	if not ok then
		write(fd, "401 Unauthorized\n")
		error(server)
	end

	if not conf.multilogin then
		if user_login[uid] then
			write(fd, "406 Not Acceptable\n")
			error(string.format("User %s is already login", uid))
		end

		user_login[uid] = true
	end

	local ok, err = pcall(conf.login_handler, server, uid, secret)
	-- unlock login
	user_login[uid] = nil

	if ok then
		err = err or ""
		write(fd,  "200 "..crypt.base64encode(err).."\n")
	else
		write(fd,  "403 Forbidden\n")
		error(err)
	end
end

local function launch_master(conf)
	local instance = conf.instance or 8
	assert(instance > 0)
	local host = conf.host or "0.0.0.0"
	local port = assert(tonumber(conf.port))
	local slave = {}
	local balance = 1

	mtask.dispatch("lua", function(_,source,command, ...)
		mtask.ret(mtask.pack(conf.command_handler(command, ...)))
	end)

	for i=1,instance do
		table.insert(slave, mtask.newservice(SERVICE_NAME))
	end

	mtask.error(string.format("login server listen at : %s %d", host, port))
	local id = socket.listen(host, port)
	socket.start(id , function(fd, addr)
		local s = slave[balance]
		balance = balance + 1
		if balance > #slave then
			balance = 1
		end
		local ok, err = pcall(accept, conf, s, fd, addr)
		if not ok then
			if err ~= socket_error then
				mtask.error(string.format("invalid client (fd = %d) error = %s", fd, err))
			end
			socket.start(fd)
		end
		socket.close(fd)
	end)
end
--构造配置表，然后调用它就可以启动一个登陆服务器。
-- local server = {
--     host = "127.0.0.1",
--     port = 8001,
--     multilogin = false, -- disallow multilogin
--     name = "login_master",
--      -- config, etc
-- }
-- login(server)

-- host 是监听地址，通常是 "0.0.0.0" 。
-- port 是监听端口。
-- name 是一个内部使用的名字，不要和 mtask 其它服务重名。在上面的例子，登陆服务器会注册为 .login_master 这个名字。
-- multilogin 是一个 boolean ，默认是 false 。关闭后，当一个用户正在走登陆流程时，禁止同一用户名进行登陆。如果你希望用户可以同时登陆，可以打开这个开关，但需要自己处理好潜在的并行的状态管理问题。
-- 同时，你还需要注册一系列业务相关的方法。

local function login(conf)
	local name = "." .. (conf.name or "login")
	mtask.start(function()
		local loginmaster = mtask.localname(name)
		if loginmaster then
			local auth_handler = assert(conf.auth_handler)
			launch_master = nil
			conf = nil
			launch_slave(auth_handler)
		else
			launch_slave = nil
			conf.auth_handler = nil
			assert(conf.login_handler)
			assert(conf.command_handler)
			mtask.register(name)
			launch_master(conf)
		end
	end)
end

return login
