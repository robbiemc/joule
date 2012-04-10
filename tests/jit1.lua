local function Dec(i)            --convert ascii to number
  if (i==nil) then
    return 0
  end

  if (i>47) then
    return i-48
  end
  return 0
end

local f = {}

for i = 1,100 do
  local a
	if i % 10 == 0 then
    a = nil
  else
    a = i
  end
  f[i] = Dec(a)
end

