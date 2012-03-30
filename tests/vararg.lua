function asdf(a, ...)
  local b, c, d = ...
  print(b,c,d)
  return a,b,c,d
end

for i = 1,2 do
  print(asdf())
  print(asdf())
  print(asdf(1))
  print(asdf(1))
  print(asdf(1,2))
  print(asdf(1,2))
  print(asdf(1,2,3))
  print(asdf(1,2,3))
  print(asdf(1,2,3,4))
  print(asdf(1,2,3,4))
  print(asdf(5,4,3,2,1))
  print(asdf(5,4,3,2,1))
end
print(asdf())

local function f(a, ...)
  return a, ...
end

for i=1,5 do
  print(f())
  print(f(1))
  print(f(1, 2))
  print(f(1, 2, 3))
end
