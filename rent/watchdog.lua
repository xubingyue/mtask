--watchdog.lua  in  rent
package.path  = "./rent/?.lua;" .. package.path

local mtask  = require "mtask"
local netpack = require "netpack"

local CMD = {}
local SOCKET = {}
local gate
local agent={}


function SOCKET.open(fd,addr)
    mtask.error("new client from :"..addr)	
	agent[fd] = mtask.newservice("agent")
	mtask.call(agent[fd],"lua","start",gate,fd)
end

local function close_agent(fd)
	local a = agent[fd]
	if a then 
		mtask.kill(a)
		agent[fd] = nil
	end
end

function SOCKET.close(fd)
	mtask.error(string.format("socket close: =>"..fd))
	close_agent(fd)
end

function SOCKET.error(fd)
	mtask.error(string.format("socket error: =>"..fd))
  	close_agent(fd)	
end

function SOCKET.data(fd,msg)
	mtask.error(string.format("socket date: =>"..fd..msg))
end

function CMD.start(conf)
	mtask.call(gate,"lua","open",conf)
end

mtask.start(function()  
	mtask.error(string.format("watchdog  start..."))
	mtask.dispatch("lua",function(session,source,cmd,subcmd,...) 
		--mtask.error(string.format(session..source..cmd..subcmd..(...)))
		if cmd == "socket" then
		    local f = SOCKET[subcmd]	
			f(...)
			-- socket api don't need return  
		else
			local f  = assert(CMD[cmd])
			mtask.ret(mtask.pack(f(subcmd,...)))
		end
	end)
	gate = mtask.newservice("gate")
end)

