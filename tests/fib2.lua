local prev, next, N, i, tmp
prev = 1
next = 1
N = 20
i = 0

while i < N do
  tmp = prev + next
  prev = next
  next = tmp
  i = i + 1
end

print(next)
