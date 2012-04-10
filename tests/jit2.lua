local bit = {}
local mod   = math.fmod

function bit.rshift(x,n)
  return math.floor(math.mod(x, 4294967296)/2^n)
end

function bit.band(x,y)
  local z,i,j = 0,1
  for j = 0,31 do
    if (mod(x,2)==1 and mod(y,2)==1) then
      z = z + i
    end
    x = bit.rshift(x,1)
    y = bit.rshift(y,1)
    i = i*2
  end
  return z
end

print(bit.band(1, 2))
print(bit.band(1, 1))
print(bit.band(3, 3))
print(bit.band(3, 3))
print(bit.band(3, 3))
print(bit.band(3, 3))

print(bit.band(3, 4))
print(bit.band(9, 4))

