-- fibonacci

-- very inefficient fibonacci function
function fib(n)
  N = N + 1
  if n < 2 then
    return n
  else
    return fib(n - 1) + fib(n - 2)
  end
end

N = 0 -- global!
local n = tonumber(arg[1] or 36)
local v = fib(n)
print(n, v)
