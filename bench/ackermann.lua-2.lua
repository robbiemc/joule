--- The Great Computer Lanuage Shootout
--  http://shootout.alioth.debian.org
--
-- Contributed by ???
-- Modified by Mike Pall (email withheld by request)
-- Submitted by Matthew Burke <shooutout@bluedino.net>
--
local function Ack(m, n)
  local ret
  if m == 0 then
    ret = n+1
  elseif n == 0 then
    ret = Ack(m-1, 1)
  else
    local t = Ack(m, n - 1)
    ret = Ack(m-1, t)
  end
  return ret
end

local N = tonumber(arg and arg[1]) or 9
io.write("Ack(3,", N ,"): ", Ack(3,N), "\n")
