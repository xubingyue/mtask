local mtask = require "mtask"
local socket = require "socket"

local function server()
	local host
	host = socket.udp(function(data, sz, from)
		local str = mtask.tostring(data,sz)	-- mtask.tostring should call only once, because it will free the pointer data
		print("server recv", str, socket.udp_address(from))
		socket.sendto(host, from, "OK " .. str)
	end , "127.0.0.1", 8765)	-- bind an address
end

local function client()
	local c = socket.udp(function(data, sz, from)
		print("client recv", mtask.tostring(data,sz), socket.udp_address(from))
	end)
	socket.udp_connect(c, "127.0.0.1", 8765)
	for i=1,20 do
		socket.write(c, "hello " .. i)	-- write to the address by udp_connect binding
	end
end

mtask.start(function()
	mtask.fork(server)
	mtask.fork(client)
end)
