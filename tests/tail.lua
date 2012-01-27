function f(a)
  return a
end

function g()
  return f(f(10))
end

print(g())
