-- $Id: ary.lua,v 1.2 2004-05-22 07:25:00 bfulgham Exp $
-- http://www.bagley.org/~doug/shootout/

local n = tonumber((arg and arg[1]) or 3000000)

local x, y = {}, {}
local last = n - 1

for i=0,last do
  x[i] = i + 1
  y[i] = 0
end
for j=last,0,-1 do
  y[j] = y[j] + x[j]
end

io.write(y[0], " ", y[last], "\n")
