a = {1,2,3,4,5}
b = {a=2, b=4, c=6, d=8, e=10, f=12}
c = {[3]=true, [6]='hello', [9]=9.33, [12]='derp'}

function dump(t, str)
  print(str ..  ' = {')
  for k,v in pairs(t) do
    print(k, ' = ', v)
  end
  print('}\n')
end

dump(a, 'a')
dump(b, 'b')
dump(c, 'c')
