-- fibonacci

-- very inefficient fibonacci function
local function fib(n)
  N = N + 1
  if n < 2 then
    return n
  else
    return fib(n - 1) + fib(n - 2)
  end
end

N = 0 -- global!
local n = tonumber(arg[1] or 24)    -- for other values, do lua fib.lua XX
local c = os.clock()
local v = fib(n)
local t = os.clock() - c
print(n, v, t, N)
