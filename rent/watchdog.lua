--watchdog.lua  in  rent
package.path  = "./rent/?.lua;" .. package.path

local skynet  = require "skynet"
local netpack = require "netpack"

local CMD = {}
local SOCKET = {}
local gate
local agent={}


function SOCKET.open(fd,addr)
    skynet.error("new client from :"..addr)	
	agent[fd] = skynet.newservice("agent")
	skynet.call(agent[fd],"lua","start",gate,fd)
end

local function close_agent(fd)
	local a = agent[fd]
	if a then 
		skynet.kill(a)
		agent[fd] = nil
	end
end

function SOCKET.close(fd)
	skynet.error(string.format("socket close: =>"..fd))
	close_agent(fd)
end

function SOCKET.error(fd)
	skynet.error(string.format("socket error: =>"..fd))
  	close_agent(fd)	
end

function SOCKET.data(fd,msg)
	skynet.error(string.format("socket date: =>"..fd..msg))
end

function CMD.start(conf)
	skynet.call(gate,"lua","open",conf)
end

skynet.start(function()  
	skynet.error(string.format("watchdog  start..."))
	skynet.dispatch("lua",function(session,source,cmd,subcmd,...) 
		--skynet.error(string.format(session..source..cmd..subcmd..(...)))
		if cmd == "socket" then
		    local f = SOCKET[subcmd]	
			f(...)
			-- socket api don't need return  
		else
			local f  = assert(CMD[cmd])
			skynet.ret(skynet.pack(f(subcmd,...)))
		end
	end)
	gate = skynet.newservice("gate")
end)

