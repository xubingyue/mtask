-- 客户端 服务器通信协议 
-- 这里用了lua来描述，在发包的时候会打包成json格式。
-- 协议第一个字段proto当做协议id来使用, 比如这里的heartbeat -> {"proto": "heartbeat", "hello": "wolrd"}
-- 由于这里是请求应答式协议，应答必须带一个"ret"字段，通常返回0表示服务器收到这个协议并且处理成功。


--目前是gate->agent->heartbeat->client，每一个连接对应一个agent，以后也可以考虑agent复用。


-------------------------- 系统使用的协议 ---------------------


-- 心跳 
proto.heartbeat = {
	proto = "heartbeat",
	hello = "wolrd",
}
proto.heartbeat_ack = {
	proto = "heartbeat_ack",
	ret = 0,
}

-------------------------- 登录系统 ------------------


------------------------ msg head ------------------------------
-- 14 byte
proto.msghead = {
	flag 		=	 "$KYJ$"  -- 5 byte
	version		=	 "1.0"    -- 1 byte
	command		= 	 "1000"	  -- 2 byte
	ret			=    "0"	  -- 2 byte 回应时，作为返回值 0：成功；非0：失败
	len			= 	 "20"	  -- 4 byte 数据包(msg--body)长度
}

------------------------msg body----------------------
proto.msgbody = {
	extra = {
		package_id   = "com.xx.x",  --项目包名标识 必填
	    version_code = 1, 			--当前app版本号 必填
		version_name = "v1.0", 		--当前版本名称 非必填
		platform 	 =  1, 			--当前平台 android  1/ ios  2 必填
		imei		 = "xxxxx",		--手机imei标识 非必填
	}
	
	-- json 串--
}
------------------------ 登录系统---------------------
proto.login = {
	id 			= 		"130" 		-- 用户id
	mobile 		= 		"mobile"	-- 手机号
	userName    =       "userName"	-- 用户名
}








