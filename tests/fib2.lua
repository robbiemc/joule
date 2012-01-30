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
local n = 36
local v = fib(n)
print(n, v)
