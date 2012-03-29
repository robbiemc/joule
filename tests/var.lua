local function g()
  return 1, 2
end

local function f()
  return 1, 2, g()
end

for i = 1,2 do
  print('a', f())
end
