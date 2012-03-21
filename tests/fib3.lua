function fib(n)
  if n < 3 then
    return 1
  end
  return fib(n - 1) + fib(n - 2)
end

print(fib(30))
