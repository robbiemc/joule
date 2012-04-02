function f(...)
  return {...}
end

for i = 1,10 do
  print(unpack(f()))
  print(unpack(f(1)))
  print(unpack(f(1, 2)))
  print(unpack(f(1, 2, 3)))
  print(unpack(f(1, 2, 3, 4)))
  print(unpack(f(1, 2, 3, 4, 5)))
  print(unpack(f(1, 2, 3, 4, 5, 6, 7)))
end
