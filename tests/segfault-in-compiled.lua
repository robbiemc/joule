function g()
  local t = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}

  local last = nil
  local f = {}

  for i = 1,100 do
    f[i] = last
    last = table.concat(t)
  end
end

for i = 1,10 do
  g()
end

