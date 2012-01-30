-- temperature conversion table (celsius to farenheit)

for c0=-20,50-1,10 do
  io.write("C ")
  for c=c0,c0+10-1 do
    io.write(string.format("%3.0f ",c))
  end
  io.write("\n")

  io.write("F ")
  for c=c0,c0+10-1 do
    f=(9/5)*c+32
    io.write(string.format("%3.0f ",f))
  end
  io.write("\n\n")
end

print(string.format("%s %q", "Hello", "Lua user!"))
print(string.format("%c%c%c", 76,117,97))
print(string.format("%e, %E", math.pi,math.pi))
print(string.format("%f, %g", math.pi,math.pi))
print(string.format("%d, %i, %u", -100,-100,-100))
print(string.format("%o, %x, %X", -100,-100,-100))
print(string.format('%q', 'a string with "quotes" and \n new line \0\\a\n\a'))
print(string.format('%q', 4))
print(string.format('%s', 10))
