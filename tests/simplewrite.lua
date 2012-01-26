function test(arg)
  io.write(arg, " : ", type(arg))
end

io.write(_VERSION, " : ", type(_VERSION))
test(3.14)
test("hi")
