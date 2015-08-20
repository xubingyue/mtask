--watchdog.lua
local skynet = require "skynet"
local netpack = require "netpack"

local CMD = {}
local SOCKET = {}
local gate 
local agent = {}


function SOCKET.open(fd,addr)
	agentp[fd] = skynet.newservice("agent")
	skynet.call(agent[fd],"lua","start",gate,fd)
	skynet.call(agent[fd],"xfs","start a new agent")
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
		skynet.kill(a)
		agent[fd] = nil
		ok,result = pcall(skynet.call,"talkbox","lua","rmUser",fd) -- 断开链接是处理用户
		
	end
end

-- test   注册新的lua协议
skynet.register_protocol {
    	name = "xfs",
		id = 12,
		pack = skynet.pack,
		unpack = skynet.unpack,
}


function CMD.start(conf)
	skynet.call(gate,"lua","open",conf)
end


skynet.start(function()
	skynet.dispatch("lua",function(session,source,cmd,subcmd,...) 
			if cmd == "socket" then
				local f  = SOCKET[subcmd]
				f(...)
			else
				local f = assert(CMD[cmd])
				skynet.ret(skynet.pack(f(subcmd,...)))
			
			end		
	end)
	gate = skynet.newservice("gate")
end)