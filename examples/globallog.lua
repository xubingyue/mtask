local mtask = require "mtask"

mtask.start(function()
	mtask.dispatch("lua", function(session, address, ...)
		print("[GLOBALLOG]", mtask.address(address), ...)
	end)
	mtask.register ".log"
	mtask.register "LOG"
end)
