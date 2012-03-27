local function f()
  return 1, 2
end

for i = 1,2 do
  print('a', f())
end
