local skynet = require "skynet"
local httpc = require "http.httpc"
local express = require "web.express"

skynet.start(function()
	-- body
	print("web http server start....")
	local web = express.app(8001,{
		web_root = "./skynet_web/www",
		thread = 2,
		static_regular = ".js|.html|.css|.pb|.png"
		})
	--web:use(".","/test/auth/tocken")
	web:listen()
	local header = {}
	local status, body = httpc.get("127.0.0.1:8001", "/test/user/index?tocken=1", {})
		print("222========",status,body)

	local status, body1 = httpc.get("127.0.0.1:8001", "/test.html", {})
	print("333========",status,body1)
	skynet.sleep(5000)
	skynet.exit()
end)