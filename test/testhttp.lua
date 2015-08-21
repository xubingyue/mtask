local mtask = require "mtask"
local httpc = require "http.httpc"
local dns = require "dns"

mtask.start(function()
	print("GET baidu.com")
	local respheader = {}
	local status, body = httpc.get("baidu.com", "/", respheader)
	print("[header] =====>")
	for k,v in pairs(respheader) do
		print(k,v)
	end
	print("[body] =====>", status)
	print(body)

	local respheader = {}
	dns.server()
	local ip = dns.resolve "baidu.com"
	print(string.format("GET %s (baidu.com)", ip))
	local status, body = httpc.get("baidu.com", "/", respheader, { host = "baidu.com" })
	print(status)

	mtask.exit()
end)
