function ternary(c, x, y)
  if c then
    return x
  else
    return y
  end
end

io.write(ternary(true, 4, "2!"), "\n")
