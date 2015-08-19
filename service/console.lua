local mtask = require "mtask"
local socket = require "socket"

local function console_main_loop()
	local stdin = socket.stdin()
	socket.lock(stdin)
	while true do
		local cmdline = socket.readline(stdin, "\n")
		if cmdline ~= "" then
			pcall(mtask.newservice,cmdline)
		end
	end
	socket.unlock(stdin)
end

mtask.start(function()
	mtask.fork(console_main_loop)
end)
