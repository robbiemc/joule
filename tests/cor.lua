function p()
  print(type(string))
  print(type(string.byte))
end

p()
string = nil

local cor = coroutine.create(p)
coroutine.resume(cor)
