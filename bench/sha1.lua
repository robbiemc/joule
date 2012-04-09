-------------------------------------------------
---      *** Crypto Test ***                  ---
-------------------------------------------------
--- Author:  Martin Huesser                   ---
--- Date:    2008-06-16                       ---
-------------------------------------------------

-------------------------------------------------
---      *** BitLibEmu for Lua ***            ---
-------------------------------------------------
--- Author:  Martin Huesser                   ---
--- Date:    2008-06-16                       ---
--- License: You may use this code in your    ---
---          projects as long as this header  ---
---          stays intact.                    ---
-------------------------------------------------

local mod   = math.fmod
local floor = math.floor
bit = {}

----------------------------------------

local function cap(x)
  return mod(x,4294967296)
end

----------------------------------------

function bit.bnot(x)
  return 4294967295-cap(x)
end

----------------------------------------

function bit.lshift(x,n)
  return cap(cap(x)*2^n)
end

----------------------------------------

function bit.rshift(x,n)
  return floor(cap(x)/2^n)
end

----------------------------------------

function bit.band(x,y)
  local z,i,j = 0,1
  for j = 0,31 do
    if (mod(x,2)==1 and mod(y,2)==1) then
      z = z + i
    end
    x = bit.rshift(x,1)
    y = bit.rshift(y,1)
    i = i*2
  end
  return z
end

----------------------------------------

function bit.bor(x,y)
  local z,i,j = 0,1
  for j = 0,31 do
    if (mod(x,2)==1 or mod(y,2)==1) then
      z = z + i
    end
    x = bit.rshift(x,1)
    y = bit.rshift(y,1)
    i = i*2
  end
  return z
end

----------------------------------------

function bit.bxor(x,y)
  local z,i,j = 0,1
  for j = 0,31 do
    if (mod(x,2)~=mod(y,2)) then
      z = z + i
    end
    x = bit.rshift(x,1)
    y = bit.rshift(y,1)
    i = i*2
  end
  return z
end


-------------------------------------------------
---      *** SHA-1 algorithm for Lua ***      ---
-------------------------------------------------
--- Author:  Martin Huesser                   ---
--- Date:    2008-06-16                       ---
--- License: You may use this code in your    ---
---          projects as long as this header  ---
---          stays intact.                    ---
-------------------------------------------------

local strlen  = string.len
local strchar = string.char
local strbyte = string.byte
local strsub  = string.sub
local floor   = math.floor
local bnot    = bit.bnot
local band    = bit.band
local bor     = bit.bor
local bxor    = bit.bxor
local shl     = bit.lshift
local shr     = bit.rshift
local h0, h1, h2, h3, h4

-------------------------------------------------

local function LeftRotate(val, nr)
  return shl(val, nr) + shr(val, 32 - nr)
end

-------------------------------------------------

local function ToHex(num)
  local i, d
  local str = ""
  for i = 1, 8 do
    d = band(num, 15)
    if (d < 10) then
      str = strchar(d + 48) .. str
    else
      str = strchar(d + 87) .. str
    end
    num = floor(num / 16)
  end
  return str
end

-------------------------------------------------

local function PreProcess(str)
  local bitlen, i
  local str2 = ""
  bitlen = strlen(str) * 8
  str = str .. strchar(128)
  i = 56 - band(strlen(str), 63)
  if (i < 0) then
    i = i + 64
  end
  for i = 1, i do
    str = str .. strchar(0)
  end
  for i = 1, 8 do
    str2 = strchar(band(bitlen, 255)) .. str2
    bitlen = floor(bitlen / 256)
  end
  return str .. str2
end

-------------------------------------------------

local function MainLoop(str)
  local a, b, c, d, e, f, k, t
  local i, j
  local w = {}
  while (str ~= "") do
    for i = 0, 15 do
      w[i] = 0
      for j = 1, 4 do
        w[i] = w[i] * 256 + strbyte(str, i * 4 + j)
      end
    end
    for i = 16, 79 do
      w[i] = LeftRotate(bxor(bxor(w[i - 3], w[i - 8]), bxor(w[i - 14], w[i - 16])), 1)
    end
    a = h0
    b = h1
    c = h2
    d = h3
    e = h4
    for i = 0, 79 do
      if (i < 20) then
        f = bor(band(b, c), band(bnot(b), d))
        k = 1518500249
      elseif (i < 40) then
        f = bxor(bxor(b, c), d)
        k = 1859775393
      elseif (i < 60) then
        f = bor(bor(band(b, c), band(b, d)), band(c, d))
        k = 2400959708
      else
        f = bxor(bxor(b, c), d)
        k = 3395469782
      end
      t = LeftRotate(a, 5) + f + e + k + w[i]  
      e = d
      d = c
      c = LeftRotate(b, 30)
      b = a
      a = t
    end
    h0 = band(h0 + a, 4294967295)
    h1 = band(h1 + b, 4294967295)
    h2 = band(h2 + c, 4294967295)
    h3 = band(h3 + d, 4294967295)
    h4 = band(h4 + e, 4294967295)
    str = strsub(str, 65)
  end
end

-------------------------------------------------

function Sha1(str)
  str = PreProcess(str)
  h0  = 1732584193
  h1  = 4023233417
  h2  = 2562383102
  h3  = 0271733878
  h4  = 3285377520
  MainLoop(str)
  return  ToHex(h0) ..
    ToHex(h1) ..
    ToHex(h2) ..
    ToHex(h3) ..
    ToHex(h4)
end

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------

s = "The quick brown fox jumps over the lazy dog"
print("Sha1('"..s.."')")
print("Expected: 2fd4e1c67a2d28fced849ee1bb76e7391b93eb12")
print("Result  : "..Sha1(s))

--Generated with RSA.java, use <BigInteger>.toString(16) for hex output.
public  = "10001"
private = "816f0d36f0874f9f2a78acf5643acda3b59b9bcda66775b7720f57d8e9015536160e72"..
"8230ac529a6a3c935774ee0a2d8061ea3b11c63eed69c9f791c1f8f5145cecc722a220d2bc7516b6"..
"d05cbaf38d2ab473a3f07b82ec3fd4d04248d914626d2840b1bd337db3a5195e05828c9abf8de8da"..
"4702a7faa0e54955c3a01bf121"
modulus = "bfedeb9c79e1c6e425472a827baa66c1e89572bbfe91e84da94285ffd4c7972e1b9be3"..
"da762444516bb37573196e4bef082e5a664790a764dd546e0d167bde1856e9ce6b9dc9801e4713e3"..
"c8cb2f12459788a02d2e51ef37121a0f7b086784f0e35e76980403041c3e5e98dfa43ab9e6e85558"..
"c5dc00501b2f2a2959a11db21f"

-------------------------------------------------
---      *** BigInteger for Lua ***           ---
-------------------------------------------------
--- Author:  Martin Huesser                   ---
--- Date:    2008-06-16                       ---
--- License: You may use this code in your    ---
---          projects as long as this header  ---
---          stays intact.                    ---
-------------------------------------------------

---------------------------------------
--- Lua 5.0/5.1/WoW Header ------------
---------------------------------------

local strlen  = string.len
local strchar = string.char
local strbyte = string.byte
local strsub  = string.sub
local max     = math.max
local min     = math.min
local floor   = math.floor
local ceil    = math.ceil
local mod     = math.fmod
local getn    = function(t) return #t end
local setn    = function() end
local tinsert = table.insert

---------------------------------------
--- Helper Functions ------------------
---------------------------------------

local function Digit(x,i)          --returns i-th digit or zero
  local d = x[i]            --if out of bounds
  if (d==nil) then
    return 0
  end
  return d
end

---------------------------------------

local function Clean(x)            --remove leading zeros
  local i = getn(x)
  while (i>1 and x[i]==0) do
    x[i] = nil
    i = i-1
  end
  setn(x,i)
end

---------------------------------------
--- String Conversion -----------------
---------------------------------------

local function Hex(i)            --convert number to ascii
  if (i>-1 and i<10) then
    return strchar(48+i)
  end
  if (i>9 and i<16) then
    return strchar(55+i)
  end
  return strchar(48)
end

---------------------------------------

local function Dec(i)            --convert ascii to number
  if (i==nil) then
    return 0
  end
  if (i>47 and i<58) then
    return i-48
  end
  if (i>64 and i<71) then
    return i-55
  end
  if (i>96 and i<103) then
    return i-87
  end
  return 0
end

---------------------------------------

function BigInt_NumToHex(x)          --convert number to hexstring
  local s,i,j,c = ""
  for i = 1,getn(x) do
    c = x[i]
    for j = 1,6 do
      s = Hex(mod(c,16))..s
      c = floor(c/16)
    end
  end
  i = 1
  while (i<strlen(s) and strbyte(s,i)==48) do
    i = i+1
  end
  return strsub(s,i)
end

---------------------------------------

function BigInt_HexToNum(h)          --convert hexstring to number
  local x,i,j = {}
  for i = 1,ceil(strlen(h)/6) do
    x[i] = 0
    for j = 1,6 do
      x[i] = 16*x[i]+Dec(strbyte(h,max(strlen(h)-6*i+j,0)))
    end
  end
  Clean(x)
  return x
end

---------------------------------------
--- Math Functions --------------------
---------------------------------------

function BigInt_Add(x,y)          --add numbers
  local z,l,i,r = {},max(getn(x),getn(y))
  z[1] = 0
  for i = 1,l do
    r = Digit(x,i)+Digit(y,i)+z[i]
    if (r>16777215) then
      z[i] = r-16777216
      z[i+1] = 1
    else
      z[i] = r
      z[i+1] = 0
    end
  end
  Clean(z)
  return z
end

---------------------------------------

function BigInt_Sub(x,y)          --subtract numbers
  local z,l,i,r = {},max(getn(x),getn(y))
  z[1] = 0
  for i = 1,l do
    r = Digit(x,i)-Digit(y,i)-z[i]
    if (r<0) then
      z[i] = r+16777216
      z[i+1] = 1
    else
      z[i] = r
      z[i+1] = 0
    end
  end
  if (z[l+1]==1) then
    return nil
  end
  Clean(z)
  return z
end

---------------------------------------

function BigInt_Mul(x,y)          --multiply numbers
  local z,t,i,j,r = {},{}
  for i = getn(y),1,-1 do
    t[1] = 0
    for j = 1,getn(x) do
      r = x[j]*y[i]+t[j]
      t[j+1] = floor(r/16777216)
      t[j] = r-t[j+1]*16777216
    end
    tinsert(z,1,0)
    z = BigInt_Add(z,t)
  end
  Clean(z)
  return z
end

---------------------------------------

local function Div2(x)            --divide number by 2, (modifies
  local u,v,i = 0            --passed number and returns
  for i = getn(x),1,-1 do          --remainder)
    v = x[i]
    if (u==1) then
      x[i] = floor(v/2)+8388608
    else
      x[i] = floor(v/2)
    end
    u = mod(v,2)
  end
  Clean(x)
  return u
end

---------------------------------------

local function SimpleDiv(x,y)          --divide numbers, result
  local z,u,v,i,j = {},0          --must fit into 1 digit!
  j = 16777216
  for i = 1,getn(y) do          --This function is costly and
    z[i+1] = y[i]          --may benefit most from an
  end              --optimized algorithm!
  z[1] = 0
  for i = 23,0,-1 do
    j = j/2
    Div2(z)
    v = BigInt_Sub(x,z)
    if (v~=nil) then
       u = u+j
      x = v
    end
  end
  return u,x
end

---------------------------------------

function BigInt_Div(x,y)          --divide numbers
  local z,u,i,v = {},{},getn(x)
  for v = 1,min(getn(x),getn(y))-1 do
    tinsert(u,1,x[i])
    i = i - 1
  end
  while (i>0) do
    tinsert(u,1,x[i])
    i = i - 1
    v,u = SimpleDiv(u,y)
    tinsert(z,1,v)
  end
  Clean(z)
  return z,u
end

---------------------------------------

function BigInt_ModPower(b,e,m)          --calculate b^e mod m
  local t,s,r = {},{1}
  for r = 1,getn(e) do
    t[r] = e[r]
  end
  repeat
    r = Div2(t)
    --print(getn(t))
    if (r==1) then
      r,s = BigInt_Div(BigInt_Mul(s,b),m)
    end
    r,b = BigInt_Div(BigInt_Mul(b,b),m)
  until (getn(t)==1 and t[1]==0)
  return s
end

---------------------------------------
--- ModPower Step Functions -----------
---------------------------------------

function BigInt_MP_StepInit(b,e,m)        --initialize nonblocking ModPower,
  local x,i = {b,{},m,{1},1}        --pass resulting table to StepExec!
  for i = 1,getn(e) do
    x[2][i] = e[i]
  end
  return x
end

---------------------------------------

function BigInt_MP_StepExec(x)          --execute next calculation step,
  local r              --finished if result~=nil.
  if (x[5]==1) then
    x[5] = 2
    r = Div2(x[2])
    if (r==1) then
      r,x[4] = BigInt_Div(BigInt_Mul(x[4],x[1]),x[3])
    end
    return nil
  end
  if (x[5]==2) then
    x[5] = 1
    r,x[1] = BigInt_Div(BigInt_Mul(x[1],x[1]),x[3])
    if (getn(x[2])==1 and x[2][1]==0) then
      x[5] = 0
      return x[4]
    end
    return nil
  end
  return nil
end

-------------------------------------------------
-------------------------------------------------
-------------------------------------------------



m = BigInt_HexToNum("FEEDBEEFBADF00D")
d = BigInt_HexToNum(public)
e = BigInt_HexToNum(private)
n = BigInt_HexToNum(modulus)

print("\nMessage = "..BigInt_NumToHex(m))
print("\nEncrypting... (this will take a few minutes)")
x = BigInt_ModPower(m,e,n)
print("Encrypted Message = "..BigInt_NumToHex(x))
print("\nDecrypting... (very fast)")
y = BigInt_ModPower(x,d,n)
print("Decrypted Message = "..BigInt_NumToHex(y))
