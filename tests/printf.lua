-- an implementation of printf

function printf(...)
  io.write(string.format(...))
end

printf("Hello %s from %s on %s\n",os.getenv"USER" or "there",'lua',os.date())

printf(os.date('!%c'))
table = os.date('!*t')

print(table.year)
print(table.month)
print(table.day)
print(table.hour)
print(table.min)
print(table.sec)
print(table.wday)
print(table.yday)
print(table.isdst)
