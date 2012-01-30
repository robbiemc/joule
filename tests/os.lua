print(os.execute(3))
print(os.execute('ls'))
print(os.execute())
print(os.execute(''))
print(os.execute('a'))
print(os.execute(nil))

os.execute('ls && ls')

os.exit(0)
