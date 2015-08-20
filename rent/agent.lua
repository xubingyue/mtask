--agent.lua in rent
package.path = "lualib/?.lua;rent/?.lua;3rd/json/?.lua"
package.cpath = "luaclib/?.so;3rd/json/?.so"

local mtask   = require "mtask"
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


mtask.register_protocol {
	name = "client",
	id 	 = mtask.PTYPE_CLIENT,
	unpack = function(msg,sz)
		print("msg===>",msg);
			local recPack = mtask.tostring(msg,sz)
			print("recPack===>",recPack)
			return recPack
		end,
	dispatch = function(session ,address,text) --这里收到包先解压
			--mtask.error(string.format("agent--->recv :"..text))
			-- 解析msg head
			data  = packdeal.unpackage(text)
			--data = text
			local result = json.decode(data)
			print("result===>",result)
			-- 解析协议并转发
			if req then
				if "heartbeat" == req["proto"] then
					mtask.send("heartbeat", "lua", "heartbeat_deal", client_fd, req)
				else
					print("no this proto")
				end
			end
	end
}

function CMD.start(gate,fd)
	client_fd = fd
	mtask.call(gate,"lua","forward",fd)
end


local function connectDB()
	mtask.newservice("rent_redis")
	--mtask.call("lua",)
end

mtask.start(function() 
	mtask.error("agent  start....")
	mtask.dispatch("lua",function(_,_,cmd,...)		
		local f = CMD[cmd]
		mtask.ret(mtask.pack(f(...)))
	end)
	-- connect redis
	
end)