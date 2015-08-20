--watchdog.lua
local mtask = require "mtask"
local netpack = require "netpack"

local CMD = {}
local SOCKET = {}
local gate 
local agent = {}


function SOCKET.open(fd,addr)
	agentp[fd] = mtask.newservice("agent")
	mtask.call(agent[fd],"lua","start",gate,fd)
	mtask.call(agent[fd],"xfs","start a new agent")
end

function SOCKET.close(fd)
	close_agent(fd)
end

function SOCKET.data(fd,msg)
	--print("[data]",fd,msg)
end

function SOCKET.error(fd,msg)
	print("socket error",fd,msg)
	close_agent(fd)
end


function close_agent()
	local a = agent[fd]
	if a then 
		mtask.kill(a)
		agent[fd] = nil
		ok,result = pcall(mtask.call,"talkbox","lua","rmUser",fd) -- 断开链接是处理用户
		
	end
end

-- test   注册新的lua协议
mtask.register_protocol {
    	name = "xfs",
		id = 12,
		pack = mtask.pack,
		unpack = mtask.unpack,
}


function CMD.start(conf)
	mtask.call(gate,"lua","open",conf)
end


mtask.start(function()
	mtask.dispatch("lua",function(session,source,cmd,subcmd,...) 
			if cmd == "socket" then
				local f  = SOCKET[subcmd]
				f(...)
			else
				local f = assert(CMD[cmd])
				mtask.ret(mtask.pack(f(subcmd,...)))
			
			end		
	end)
	gate = mtask.newservice("gate")
end)