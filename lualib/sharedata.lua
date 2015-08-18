
--sharedata 正是为了大量共享结构化数据却不常更新它们，这种需求而设计出来的。

--sharedata 只支持在同一节点内（同一进程下）共享数据，如果需要跨节点，需要自行同步处理。

local mtask = require "mtask"
local sd  = require "sharedata.corelib"

local service 

mtask.init(function()
	service = mtask.uniqueservice "sharedatad"
end)

local sharedata = {}
--monitor
local function monitor(name,obj,cobj)	
	local  newobj = cobj
	while true do
		newobj = mtask.call(service,"lua","monitor",name,newobj)
		if newobj==nil then
			break
		end
		sd.update(obj,newobj)
	end
end
--获取当前节点的共享数据对象。
function sharedata.query(name)
	local obj = mtask.call(service,"lua","query",name)
	local r = sd.box(obj)
	mtask.send(service,"lua","confirm",obj)
	mtask.fork(monitor,name,r,obj)
	return r
end

--在当前节点内创建一个共享数据对象。value 可以是一张 lua table ，但不可以有环。且 key 必须是字符串和正整数。
function sharedata.new(name, v)
	mtask.call(service, "lua", "new", name, v)
end
--更新当前节点的共享数据对象。
function sharedata.update(name, v)
	mtask.call(service, "lua", "update", name, v)
end
--删除当前节点的共享数据对象。
function sharedata.delete(name)
	mtask.call(service, "lua", "delete", name)
end

return sharedata