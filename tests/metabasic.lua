t = {}
m = {}

setmetatable(t, m)

m.__add = function(a,b)
  print(type(a), type(b))
  return 2.7
end

print(t + 44)
print(44 + t)
print(t + '4')
print(t + t)
print(40 + t)
print('hi' + t)
print('3.14' + t)
print(t + true)
