function maths(a,b)
  return a+b, a-b, a*b, a/b, a%b, a^b, -a
end

print(maths(4,6))
print('')
print(math.sin(4))

print('2.34', tonumber(2.34))
print('2', tonumber('2'))
print(nil, tonumber('2b'))
print(tonumber('2b asd'))
print(tonumber('2 3b fsd'))
print(tonumber('20', 3))
print(tonumber('20.0', 4))
-- print(tonumber('10.01', 2))

print('0x20' + 1)

print(tonumber(' 10 '))
print(-'2')