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

print(xpcall(function(a)
  string.format()
end, function(e)
  print(e)
  return 1, 2
end))

print(pcall(function()

  -- print 'a'

  print(pcall(function()
    string.format()
  end))

  print(pcall(function()
    string.format()
  end))

  -- print 'here'

  string.format()
end))

print(xpcall(function()
  return 'a'
end, function() end))

print(xpcall(function()
  string.format()
end,
function()
  string.format()
end))
