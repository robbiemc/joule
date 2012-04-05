function f()
  return 1
end

function g()
  return f() + f()
end

for i = 1,20000 do
  local f = g()
  if i % 1000 == 0 then
    print(f)
  end
end

