local mtask = require "mtask"
local core = require "mtask.core"
require "mtask.manager"	-- import manager apis
local string = string

local services = {}
local command = {}
local instance = {} -- for confirm (function command.LAUNCH / command.ERROR / command.LAUNCHOK)

local function handle_to_address(handle)
	return tonumber("0x" .. string.sub(handle , 2))
end

local NORET = {}

function command.LIST()
	local list = {}
	for k,v in pairs(services) do
		list[mtask.address(k)] = v
	end
	return list
end

function command.STAT()
	local list = {}
	for k,v in pairs(services) do
		local stat = mtask.call(k,"debug","STAT")
		list[mtask.address(k)] = stat
	end
	return list
end

function command.KILL(_, handle)
	handle = handle_to_address(handle)
	mtask.kill(handle)
	local ret = { [mtask.address(handle)] = tostring(services[handle]) }
	services[handle] = nil
	return ret
end

function command.MEM()
	local list = {}
	for k,v in pairs(services) do
		local kb, bytes = mtask.call(k,"debug","MEM")
		list[mtask.address(k)] = string.format("%.2f Kb (%s)",kb,v)
	end
	return list
end

function command.GC()
	for k,v in pairs(services) do
		mtask.send(k,"debug","GC")
	end
	return command.MEM()
end

function command.REMOVE(_, handle, kill)
	services[handle] = nil
	local response = instance[handle]
	if response then
		-- instance is dead
		response(not kill)	-- return nil to caller of newservice, when kill == false
		instance[handle] = nil
	end

	-- don't return (mtask.ret) because the handle may exit
	return NORET
end

local function launch_service(service, ...)
	local param = table.concat({...}, " ")
	local inst = mtask.launch(service, param)
	local response = mtask.response()
	if inst then
		services[inst] = service .. " " .. param
		instance[inst] = response
	else
		response(false)
		return
	end
	return inst
end

function command.LAUNCH(_, service, ...)
	launch_service(service, ...)
	return NORET
end

function command.LOGLAUNCH(_, service, ...)
	local inst = launch_service(service, ...)
	if inst then
		core.command("LOGON", mtask.address(inst))
	end
	return NORET
end

function command.ERROR(address)
	-- see serivce-src/service_lua.c
	-- init failed
	local response = instance[address]
	if response then
		response(false)
		instance[address] = nil
	end
	services[address] = nil
	return NORET
end

function command.LAUNCHOK(address)
	-- init notice
	local response = instance[address]
	if response then
		response(true, address)
		instance[address] = nil
	end

	return NORET
end

-- for historical reasons, launcher support text command (for C service)

mtask.register_protocol {
	name = "text",
	id = mtask.PTYPE_TEXT,
	unpack = mtask.tostring,
	dispatch = function(session, address , cmd)
		if cmd == "" then
			command.LAUNCHOK(address)
		elseif cmd == "ERROR" then
			command.ERROR(address)
		else
			error ("Invalid text command " .. cmd)
		end
	end,
}

mtask.dispatch("lua", function(session, address, cmd , ...)
	cmd = string.upper(cmd)
	local f = command[cmd]
	if f then
		local ret = f(address, ...)
		if ret ~= NORET then
			mtask.ret(mtask.pack(ret))
		end
	else
		mtask.ret(mtask.pack {"Unknown command"} )
	end
end)

mtask.start(function() end)
