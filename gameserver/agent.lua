--agent.lua
package.path = "lualib/?.lua;gameserver/?.lua;3rd/json/?.lua"
package.cpath = "luaclib/?.so;3rd/json/?.so"

local skynet   = require "skynet"
local socket   = require "socket"
local json     = require "json"


local CMD = {}
local client_id -- save client_id


function pack_json_deall(req)
	if req then
		print("proto: "..req["proto"])
		if "heartbeat" == req["proto"] then
			skynet.send("heartbeat", "lua", "heartbeat_deal", client_fd, req)
		else
			print("no this proto")
		end
	end
end


function unpack_package(text)
	print("package.unpack_package==>",text,type(text),#text)
	local size = #text
	if size < 2 then 
		return nil
	end
	local s = text:byte(1) * 256 + text:byte(2)
	print("头两个字节＝＝＝>",tonumber(s))
	if size < s + 2 then 
		return nil
	end
	
	return  json.decode(text:sub(3,2+s))
end



skynet.register_protocol {
	name = "client",
	id 	 = skynet.PTYPE_CLIENT,
	unpack = function(msg,sz)
		print("agent   => msg",msg,sz)
		local recPack = skynet.tostring(msg,sz)
		print("recPack===>",recPack)
			return skynet.tostring(msg,sz)
		end,
		
	dispatch = function(session ,address,text)
			--这里收到包先解压
			print("agent--->recv :",text)
			unpack_package(text)
			local req = json.decode(text)
			print("agent--->req :",req)
			pack_json_deall(req)
	end
}

function CMD.start(gate,fd)
	client_fd = fd
	skynet.call(gate,"lua","forward",fd)
end


skynet.start(function() 
	skynet.error("agent  start....")
	skynet.dispatch("lua",function(_,_,cmd,...)
		print("agent cmd===>",cmd,...)
		
		local f = CMD[cmd]
		skynet.ret(skynet.pack(f(...)))
	end)
end)