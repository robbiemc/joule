function f(a, b, c)
  return a, b
end

-- a,b,c = f(10, 2, 1)
-- print(c)
-- --print(a,b)
-- print(f(10,2,1))

function g(a, b, c)
  print(a)
  print(b)
  print(c)
end

g(10, f(10, 1, 2))
