local mtask = require "mtask"

local clusterd
local cluster = {}


function cluster.call(node,address,...)
	-- mtask.pack(...) will free by cluster.core.packrequest
	return mtask.call(clusterd,"lua", "req", node, address, mtask.pack(...))
end
function cluster.open(port)
	if type(port)=="string" then
		mtask.call(clusterd,"lua","listen",port)
	else
		mtask.call(clusterd,"lua","listen","0.0.0.0", port)
	end
end

function cluster.reload()
	mtask.call(clusterd,"lua","reload")
end

function cluster.proxy(node,name)
	 mtask.call(clusterd,"lua","proxy",node,name)
end

return cluster