--packdeal.lua  in rent


package.cpath = "luaclib/?.so;3rd/json/?.so"
package.path = "lualib/?.lua;3rd/json/?.lua;rent/?.lua"

local socket = require "socket"
local bit32 = require "bit32"
local json = require "json"

local packdeal = {}

-- we  use 2 bytes  save pack_len in pack  head  

function packdeal.send_package(client_id,ack)
	local pack = json.encode(ack)
	print("send: "..pack)
	
	-- zip
	local size = #pack
	local package = string.char(bit32.extract(size,8,8))..
					string.char(bit32.extract(size,0,8))..
					pack

	socket.write(client_id,package)
end


local function unpack_msghead(text)
	local flag =  string.sub(text,1,5)  
	print("flag==>",flag)
	local version =  string.sub(text,6,1)
	print("version==>",version)
	local command =  string.sub(text,7,2)  
	print("command==>",version)
	local ret =  string.sub(text,9,2)  
	print("ret==>",ret)
	local len =  string.sub(text,10,4)  
	print("len==>",len)
	return len
end
function package.unpack_package(text)
	assert(text,"text is  nill ...")
	if not text then 
		print("text  is  nil")
	end
	mtask.error("package.unpack_package==>"..text)
	local size = #text

	if size < 14 then   
		return nil
	end
	 
	local msgbodylen = unpack_msghead(text)
	print("msgbodylen===",msgbodylen)
	
	if size < msgbodylen + 14 then 
		return nil
	end
	
	return  json.decode(text:sub(15,size))
end



return  packdeal