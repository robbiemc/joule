-- The Computer Language Benchmarks Game
-- http://shootout.alioth.debian.org/
-- contributed by Mike Pall

local function BottomUpTree(item, depth)
  if depth > 0 then
    local i = item + item
    depth = depth - 1
    local left, right = BottomUpTree(i-1, depth), BottomUpTree(i, depth)
    return { item, left, right }
  else
    return { item }
  end
end

local function ItemCheck(tree)
  if tree[2] then
    return tree[1] + ItemCheck(tree[2]) - ItemCheck(tree[3])
  else
    return tree[1]
  end
end

local N = tonumber(arg and arg[1]) or 10
local mindepth = 4
local maxdepth = mindepth + 2

for depth=mindepth,maxdepth,2 do
  local iterations = 2 ^ (maxdepth - depth + mindepth)
  for i=1,iterations do
    ItemCheck(BottomUpTree(1, depth))
    ItemCheck(BottomUpTree(-1, depth))
  end
end
