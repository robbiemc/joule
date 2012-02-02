setmetatable(_G, {
  __newindex = function() end,
  __index = function() return math.random() end
})

-- locals are ok
local a = 4
a = 5
-- but you can't create any globals!
b = 'wrong'
print(b)
print(c)
