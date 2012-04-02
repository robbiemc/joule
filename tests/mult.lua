function mult(x, c, a)
  if x == 0 then
    return a
  end
  return mult(x-1, c, a+c)
end

local a,b,c = mult(5,4,0), mult(6,8,0), mult(2,4,0)
print(a,b,c)
