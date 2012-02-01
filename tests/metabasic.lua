s = {}
t = {}
ms = {}
mt = {}

setmetatable(s, ms)
setmetatable(t, mt)

mt.__add = function(a,b)
  print(type(a), type(b))
  return 2.7
end
mt.__eq = function(a,b)
  print('eq')
  return nil
end
ms.__eq = mt.__eq

print(t + 44)
print(44 + t)
print(t + '4')
print(t + t)
print(40 + t)
print('hi' + t)
print('3.14' + t)
print(t + true)

if t == s then
  print('equal')
else
  print('not equal')
end
