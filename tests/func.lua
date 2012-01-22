function foo(x, y)
  local bar = function(y) return x+y end
  local n = bar(y+1)
  return n-1
end

print(foo(4,6))
