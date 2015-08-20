--packdeal.lua 

package.cpath = "luaclib/?.so;3rd/json/?.so"
package.path = "lualib/?.lua;3rd/json/?.lua;gameserver/?.lua"

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

function packdeal.unpack_package(text)
	--mtask.error("package.unpack_package==>"..text)
	print("package.unpack_package==>",text)
	local size = #text

	if size < 2 then 
		return nil
	end
	
	
	print("text type is :" , type(text))
	local s = text:byte(1) * 256 + text:byte(2)
	
	if size < s + 2 then 
		return nil
	end
	
	return  json.decode(text:sub(3,2+s))
end



return  packdeal