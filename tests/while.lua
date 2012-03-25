local i,j,k = 0,0,0
while i < 10 do
  i = i+1
  j = 0
  while j < 10 do
    j = j+1
    k = 0
    while k < 10 do
      k = k+1
      if i+j+k == 7 then
        print(i,j,k)
      end
    end
  end
end
