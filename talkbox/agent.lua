--agent.lua

local mtask = require "mtask"
local jsonpack = require "jsonpack"
local netpack = require "netpack"
local socket = require "socket"
p=require("p.core")

local CMD = {}

local client_fd

local function xfs_send(v)
	data = p.unpack(v)
	print("send ok",data.v,data.p)
	socket.write(client_fd, netpack.pack(v))
end

mtask.register_protocol {
	name = "client",
	id = mtask.PTYPE_CLIENT,
	unpack = function (msg, sz)
		return mtask.tostring(msg,sz)
	end,
	dispatch = function (session, address, text)
		data = p.unpack(text)
		print("receive ok",data.v,data.p)
		local ok,result
		if data.p == 1001 then--获取用户列表
			ok, result = pcall(mtask.call,"talkbox", "lua", "getUsers", data.msg)
			if ok then
				xfs_send(p.pack(1,1002,result))
			else
				print("error:",data.p)
			end
		elseif data.p == 1003 then--创建用户
			ok, result = pcall(mtask.call,"talkbox", "lua", "createUser", client_fd, data.msg)
			if ok then
				xfs_send(p.pack(1,1000,result))
			else
				print("error:",data.p)
			end
		elseif data.p == 1005 then--发送消息
			ok, result = pcall(mtask.call,"talkbox", "lua", "sentMsg", data.msg)
			if ok then
				xfs_send(p.pack(1,1000,result))
			else
				print("error:",data.p)
			end
		else
			xfs_send(p.pack(1,0,data.msg.."\0"))
		end
	end
}

mtask.register_protocol {
	name = "xfs",
	id = 12,
	pack = mtask.pack,
	unpack = mtask.unpack,
	dispatch = function (session, address, text)
		print("[LOG]", mtask.address(address),text)
		xfs_send(p.pack(1,0,"Welcome to mtask\n"))
		mtask.retpack(text)
	end
}

function CMD.start(gate , fd)
	client_fd = fd
	mtask.call(gate, "lua", "forward", fd)
end

mtask.start(function()
	mtask.dispatch("lua", function(_,_, command, ...)
		local f = CMD[command]
		mtask.ret(mtask.pack(f(...)))
	end)
end)