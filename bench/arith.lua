-- a test that trys to avoid using tables
local s = 0
local n = tonumber(arg[1] or 4000000)
for x=0,n do
  s = s + x*x*x - 8*x*x - 44*x + 28.3 - s / 1.3
end
print(s)
