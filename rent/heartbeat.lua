--heartbeat.lua  in rent   by TTc 
--heartbeat.lua  心跳包

package.path = "lualib/?.lua;rent/?.lua"

local mtask	= require "mtask"
local netpack	= require "netpack"
local socket	= require "socket"
local packdeal  = require "packdeal"

local CMD = {}

function CMD.heartbeat_deal(client_fd, req_msg)
	local ack = {}
	ack["proto"] = "heartbeat_ack"
	ack["ret"] = 0
	packdeal.send_package(client_fd, ack)
end

mtask.start(function()
	mtask.dispatch("lua", function(session, address, cmd, ...)
		local f = CMD[cmd]
		f(...)
	end)
	mtask.register "heartbeat"
end)