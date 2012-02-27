coroutine.wrap(function()
  a = 'a'
  for i = 1, 10000 do
    a = a..'a'
  end
end)()
