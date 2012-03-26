local i,a,b,c = 0,1,2,3

function stuff(d)
  a = b+1
  b = c+1
  c = d+1
  return c + 1
end

while i < 300 do
  i = i + (a+b/c)
  local x = stuff(i)
  print(x)
end
