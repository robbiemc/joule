local i,j,k = 0,0,0
while i < 1000 do
  i = i+1
  j = 0
  while j < 100 do
    j = j+1
    k = 0
    while k < 100 do
      k = k+1
      if i+j+k == 7 then
        print(i,j,k)
      end
    end
  end
end
