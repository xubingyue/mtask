local mtask = require "mtask"
local harbor = require "mtask.harbor"

mtask.start(function()
	local standalone =mtask.getenv "standalone"

	local launcher =assert(mtask.launch("snlua","launcher"))

	mtask.name(".launcher",launcher)

	local harbor_id =tonumber(mtask.getenv "harbor")

	if harbor_id==0 then--单节点网络
		assert(standalone==nil)
		standalone=true--启动datacenterd
		mtask.setenv("standalone","true")
		--启动cdummy服务，拦截对外广播的全局名字变更
		local ok,slave = pcall(mtask.newservice,"cdummy")
		if not ok then
			mtask.abort()
		end
		mtask.name(".cslave",slave)

	else--多节点网络
	   --如果是master节点
		if standalone then
			--启动cmaster服务，做节点调度,协调组网
			if not pcall(mtask.newservice,"cmaster") then
				mtask.abort()
			end
		end
		--无论是master节点还是slave节点，都有slave服务
		mtask.name(".cslave",slave)
	end
	--单节点网络会启动该服务
	--多节点网络master节点会启动该服务
	if standalone then
		local datacenter =mtask.newservice "datacenter"
		mtask.name("DATACENTER",datacenter)
	end
	mtask.newservice "service_mgr"--管理UniqueService的服务

	pcall(mtask.newservice,mtask.getenv "start" or "main")
	mtask.exit() --bootstrap exit
end)
