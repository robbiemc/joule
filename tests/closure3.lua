function f(n)
  while n > 0 do
    local a = 'foo'
    function unused() a = n end
    n = n-1
  end
end

f(1)
