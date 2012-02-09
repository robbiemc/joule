#!/usr/bin/env lua

RED   = "\27[;31m"
GREEN = "\27[;32m"
RESET = "\27[;0m"

function time(interpreter)
  local format = '"\\n%U\\n%S\\n%M\\n%R"'
  local cmd = string.format('/usr/bin/time -o /dev/stdout -f %s %s luac.out',
                            format, interpreter)
  local f = io.popen(cmd)
  local lines = {0,0,0,0}
  for line in f:lines() do
    table.remove(lines, 1)
    lines[4] = line
  end
  return unpack(lines)
end

function printPercentage(l, j)
  local pct = (j - l) / l * 100
  local color = RESET
  if pct > 15 then color = RED end
  if pct < 0  then color = GREEN end
  io.write(string.format("%s%7.1f%%%s\t", color, pct, RESET))
end

print('                       Test         Time             Usr             Mem          Faults')
table.sort(arg)
for _,f in ipairs(arg) do
  os.execute('luac ' .. f)

  local lu,ls,lm,lf = time('lua')
  local ju,js,jm,jf = time('./joule')
  io.write(string.format('%27s\t', f))
  printPercentage(lu+ls, ju+js)
  printPercentage(lu, ju)
  printPercentage(lm, jm)
  printPercentage(lf, jf)
  io.write("\n")
end
