local prev, next, N, i, tmp, j
prev = 1
next = 1
N = 3000000
i = 0
j = nil

while i < N do
  tmp = prev + next
  prev = next
  next = tmp
  i = i + 1
end

print(next, j)
