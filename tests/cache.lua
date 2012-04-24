a = 2
local b = 0
for i = 0, 1000000 do
  b = b + a
end
print(b)

t = {a = 2, b = 4}
local c = 0
for i = 0, 1000000 do
  c = c + t.a + t.b
end
print(c)
