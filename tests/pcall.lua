function f()
  string.format()
end

print(pcall(f))

print(pcall(function()
  return 1
end))

print(pcall(function(a)
  return a
end, 10))

print(pcall(function(a)
  string.format()
end, 10))
