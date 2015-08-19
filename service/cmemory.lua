local mtask = require "mtask"
local memory = require "memory"

memory.dumpinfo()
memory.dump()

print("Total memory:", memory.total())
print("Total block:", memory.block())

mtask.start(function() mtask.exit() end)