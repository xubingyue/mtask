--agent.lua in rent
package.path = "lualib/?.lua;rent/?.lua;3rd/json/?.lua"
package.cpath = "luaclib/?.so;3rd/json/?.so"

local skynet   = require "skynet"
local socket   = require "socket"
local json     = require "json"
local packdeal = require "packdeal"


local CMD = {}
local client_id -- save client_id

function unpackage(text)
	local size = #text
	if size < 14 then 
		return nil
	end
	local s = text:byte(1) 
end


skynet.register_protocol {
	name = "client",
	id 	 = skynet.PTYPE_CLIENT,
	unpack = function(msg,sz)
		print("msg===>",msg);
			local recPack = skynet.tostring(msg,sz)
			print("recPack===>",recPack)
			return recPack
		end,
	dispatch = function(session ,address,text) --这里收到包先解压
			--skynet.error(string.format("agent--->recv :"..text))
			-- 解析msg head
			data  = packdeal.unpackage(text)
			--data = text
			local result = json.decode(data)
			print("result===>",result)
			-- 解析协议并转发
			if req then
				if "heartbeat" == req["proto"] then
					skynet.send("heartbeat", "lua", "heartbeat_deal", client_fd, req)
				else
					print("no this proto")
				end
			end
	end
}

function CMD.start(gate,fd)
	client_fd = fd
	skynet.call(gate,"lua","forward",fd)
end


local function connectDB()
	skynet.newservice("rent_redis")
	--skynet.call("lua",)
end

skynet.start(function() 
	skynet.error("agent  start....")
	skynet.dispatch("lua",function(_,_,cmd,...)		
		local f = CMD[cmd]
		skynet.ret(skynet.pack(f(...)))
	end)
	-- connect redis
	
end)