--client.lua  in rent  for  test   by  TTc
package.cpath = "luaclib/?.so;3rd/json/?.so"
package.path = "lualib/?.lua;rent/?.lua;3rd/json/?.lua"

local socket = require "clientsocket"
local bit32  = require "bit32"
local json   = require "json"

local fd = assert(socket.connect("127.0.0.1", 8888))

-- 这里发送包的时候会加上14个字节的头部来表示包的长度
--  flag 5byte  +  version  1byte  +  cmd 2byte + ret 2byte + len 4byte
local msghead = {
		flag 		=	 "$KYJ$"  -- 5 byte
		version		=	 1    -- 1 byte
		command		= 	 1000	  -- 2 byte
		ret			=    "0"	  -- 2 byte 回应时，作为返回值 0：成功；非0：失败
		len			= 	 "20"	  -- 4 byte 数据包(msg--body)长度
}

local extra = {
	package_id   = "com.kuaiyoujia.rent",  --项目包名标识 必填
    version_code = 1, 			--当前app版本号 必填
	version_name = "v1.0", 		--当前版本名称 非必填
	platform 	 =  1, 			--当前平台 android  1/ ios  2 必填
	imei		 = "xxxxx",		--手机imei标识 非必填
}
--print("extra==>",json.encode(extra))
local function send_package(fd, pack)
	pack  =  pack..json.encode(extra)
	local size = #pack
	print("size===>",size)
	local msgbody = string.char(bit32.extract(size,24,8)) ..
		string.char(bit32.extract(size,16,8)) ..
		string.char(bit32.extract(size,8,8)) ..
		string.char(bit32.extract(size,0,8))..
		pack
		
		local package = 
	print("package content==>",package,#package)
	socket.send(fd, package)
end

-- 解包先处理头部的2字节长度
local function unpack_package(text)
	print("text==>",text)
	local size = #text
	if size < 2 then
		return nil, text
	end
	local s = text:byte(1) * 256 + text:byte(2)
	if size < s+2 then
		return nil, text
	end

	return text:sub(3,2+s)
end

local send_msg ={}

send_msg["userName"] = "TTc"
send_msg["password"] = "123123"
print("send: ", json.encode(send_msg)) --  {"proto":"heartbeat","hello":"world"}

send_package(fd, json.encode(send_msg))

while true do
	local recv_buf = socket.recv(fd)
	if recv_buf then
		local ret_msg = json.decode(unpack_package(recv_buf)) --  --  {"ret":0,"proto":"heartbeat_ack"} 客户端收到应答包先处理2字节的头部，再json解包成lua table。
		if ret_msg then
			print("proto: "..ret_msg["proto"])
			print("ret  : "..ret_msg["ret"])
		end
	end
end

socket.close(fd)
 
