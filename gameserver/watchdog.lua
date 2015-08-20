--watchdog.lua
package.path = "./gameserver/?.lua;" .. package.path

local mtask = require "mtask"
local netpack = require "netpack"

local CMD = {}
local SOCKET = {}
local gate
local agent={}


function SOCKET.open(fd,addr)
    mtask.error("new client from :"..addr)	agent[fd] = mtask.newservice("agent")

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
	print("socket close: =>",fd)
	close_agent(fd)
end

function SOCKET.error(fd)
	print("socket error: =>",fd)
  	close_agent(fd)	
end

function SOCKET.data(fd,msg)
	print("socket date==>",fd,msg)
end


function CMD.start(conf)
	mtask.call(gate,"lua","open",conf)
end



mtask.start(function()  
	print("watchdog  mtask.start() called...")
	mtask.dispatch("lua",function(session,source,cmd,subcmd,...) 
        print("\t222=>服务地址＝=",mtask.self()," session => ",session,"source =>",source," cmd=>",cmd ,"subcmd => ",subcmd,"^^^ ...=>",...)
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

