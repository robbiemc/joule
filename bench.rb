#!/usr/bin/env ruby

RED = "\e[;31m"
GREEN = "\e[;32m"
RESET = "\e[;0m"

ARGV.sort.each do |file|
  if file == nil || !File.exist?(file)
    puts "Unknown file: #{file}"
    exit 1
  end

  t1 = Time.now
  lua_out = `lua #{file}`
  lua_time = Time.now - t1

  system "luac #{file}"
  t1 = Time.now
  joule_out = `./joule -c luac.out`
  joule_time = Time.now - t1
  system 'rm luac.out'

  if lua_out != joule_out
    puts "Different output, bad test run"
    exit 1
  end

  pct = (joule_time - lua_time) / lua_time * 100
  if pct > 15
    pct = sprintf "#{RED}%10.2f%%#{RESET}", pct
  elsif pct < 0
    pct = sprintf "#{GREEN}%10.2f%%#{RESET}", pct
  else
    pct = sprintf "#{RESET}%10.2f%%#{RESET}", pct
  end

  printf "%25s\tl:%10f\tj:%10f\t%s\n", file, lua_time, joule_time, pct
end
