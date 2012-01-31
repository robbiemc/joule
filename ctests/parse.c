#include <assert.h>

#include "parse.h"

/*
    puts File.read('tests/fib.luac').bytes.to_a.map{ |b|
      sprintf "\\x%02x", b
    }.each_slice(18){ |s| puts '"' + s.join + '"' }
 */
char to_parse[] =
  "\x1b\x4c\x75\x61\x51\x00\x01\x04\x08\x04\x08\x00\x0f\x00\x00\x00\x00\x00"
  "\x00\x00\x40\x74\x65\x73\x74\x73\x2f\x66\x69\x62\x2e\x6c\x75\x61\x00\x00"
  "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x02\x06\x24\x00\x00\x00\x24\x00\x00"
  "\x00\x07\x00\x00\x00\x24\x40\x00\x00\x07\x40\x00\x00\x24\x80\x00\x00\x07"
  "\x80\x00\x00\x05\x00\x01\x00\x06\x40\x41\x00\x1a\x40\x00\x00\x16\x00\x00"
  "\x80\x01\x80\x01\x00\x07\xc0\x00\x00\x05\xc0\x01\x00\x45\xc0\x00\x00\x1c"
  "\x80\x00\x01\x07\xc0\x00\x00\x05\x00\x02\x00\x41\x40\x02\x00\x81\xc0\x00"
  "\x00\xc1\x80\x02\x00\x01\xc1\x02\x00\x41\x01\x03\x00\x1c\x40\x00\x03\x05"
  "\x80\x00\x00\x41\x40\x03\x00\x85\x00\x00\x00\x1c\x40\x80\x01\x05\x40\x00"
  "\x00\x45\x00\x00\x00\x1c\x80\x00\x01\x07\x00\x00\x00\x05\x80\x00\x00\x41"
  "\x80\x03\x00\x85\x00\x00\x00\x1c\x40\x80\x01\x1e\x00\x80\x00\x0f\x00\x00"
  "\x00\x04\x04\x00\x00\x00\x00\x00\x00\x00\x66\x69\x62\x00\x04\x06\x00\x00"
  "\x00\x00\x00\x00\x00\x63\x61\x63\x68\x65\x00\x04\x05\x00\x00\x00\x00\x00"
  "\x00\x00\x74\x65\x73\x74\x00\x04\x02\x00\x00\x00\x00\x00\x00\x00\x6e\x00"
  "\x04\x04\x00\x00\x00\x00\x00\x00\x00\x61\x72\x67\x00\x03\x00\x00\x00\x00"
  "\x00\x00\xf0\x3f\x03\x00\x00\x00\x00\x00\x00\x38\x40\x04\x09\x00\x00\x00"
  "\x00\x00\x00\x00\x74\x6f\x6e\x75\x6d\x62\x65\x72\x00\x04\x06\x00\x00\x00"
  "\x00\x00\x00\x00\x70\x72\x69\x6e\x74\x00\x04\x01\x00\x00\x00\x00\x00\x00"
  "\x00\x00\x04\x06\x00\x00\x00\x00\x00\x00\x00\x76\x61\x6c\x75\x65\x00\x04"
  "\x05\x00\x00\x00\x00\x00\x00\x00\x74\x69\x6d\x65\x00\x04\x06\x00\x00\x00"
  "\x00\x00\x00\x00\x65\x76\x61\x6c\x73\x00\x04\x06\x00\x00\x00\x00\x00\x00"
  "\x00\x70\x6c\x61\x69\x6e\x00\x04\x07\x00\x00\x00\x00\x00\x00\x00\x63\x61"
  "\x63\x68\x65\x64\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x04"
  "\x00\x00\x00\x0b\x00\x00\x00\x00\x01\x00\x04\x10\x00\x00\x00\x45\x00\x00"
  "\x00\x4c\x40\xc0\x00\x47\x00\x00\x00\x18\x80\x40\x00\x16\x40\x00\x80\x1e"
  "\x00\x00\x01\x16\xc0\x01\x80\x45\xc0\x00\x00\x8d\x40\x40\x00\x5c\x80\x00"
  "\x01\x85\xc0\x00\x00\xcd\x80\x40\x00\x9c\x80\x00\x01\x4c\x80\x80\x00\x5e"
  "\x00\x00\x01\x1e\x00\x80\x00\x04\x00\x00\x00\x04\x02\x00\x00\x00\x00\x00"
  "\x00\x00\x4e\x00\x03\x00\x00\x00\x00\x00\x00\xf0\x3f\x03\x00\x00\x00\x00"
  "\x00\x00\x00\x40\x04\x04\x00\x00\x00\x00\x00\x00\x00\x66\x69\x62\x00\x00"
  "\x00\x00\x00\x10\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00\x00\x05\x00\x00"
  "\x00\x06\x00\x00\x00\x06\x00\x00\x00\x07\x00\x00\x00\x07\x00\x00\x00\x09"
  "\x00\x00\x00\x09\x00\x00\x00\x09\x00\x00\x00\x09\x00\x00\x00\x09\x00\x00"
  "\x00\x09\x00\x00\x00\x09\x00\x00\x00\x09\x00\x00\x00\x0b\x00\x00\x00\x01"
  "\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x6e\x00\x00\x00\x00\x00\x0f"
  "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0e\x00\x00"
  "\x00\x18\x00\x00\x00\x00\x01\x00\x03\x06\x00\x00\x00\x4a\x00\x00\x00\xa4"
  "\x00\x00\x00\x00\x00\x80\x00\x00\x00\x00\x00\x9e\x00\x00\x01\x1e\x00\x80"
  "\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x10"
  "\x00\x00\x00\x17\x00\x00\x00\x02\x01\x00\x04\x0c\x00\x00\x00\x44\x00\x00"
  "\x00\x46\x00\x80\x00\x5a\x40\x00\x00\x16\x40\x01\x80\x84\x00\x80\x00\xc0"
  "\x00\x00\x00\x9c\x80\x00\x01\x40\x00\x00\x01\x84\x00\x00\x00\x89\x40\x00"
  "\x00\x5e\x00\x00\x01\x1e\x00\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0c"
  "\x00\x00\x00\x11\x00\x00\x00\x11\x00\x00\x00\x12\x00\x00\x00\x12\x00\x00"
  "\x00\x13\x00\x00\x00\x13\x00\x00\x00\x13\x00\x00\x00\x13\x00\x00\x00\x14"
  "\x00\x00\x00\x14\x00\x00\x00\x16\x00\x00\x00\x17\x00\x00\x00\x02\x00\x00"
  "\x00\x02\x00\x00\x00\x00\x00\x00\x00\x78\x00\x00\x00\x00\x00\x0b\x00\x00"
  "\x00\x02\x00\x00\x00\x00\x00\x00\x00\x79\x00\x02\x00\x00\x00\x0b\x00\x00"
  "\x00\x02\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x63\x00\x02\x00\x00"
  "\x00\x00\x00\x00\x00\x66\x00\x06\x00\x00\x00\x0f\x00\x00\x00\x17\x00\x00"
  "\x00\x17\x00\x00\x00\x17\x00\x00\x00\x17\x00\x00\x00\x18\x00\x00\x00\x02"
  "\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x66\x00\x00\x00\x00\x00\x05"
  "\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\x63\x00\x01\x00\x00\x00\x05"
  "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x1b\x00\x00"
  "\x00\x21\x00\x00\x00\x00\x02\x00\x0b\x14\x00\x00\x00\x81\x40\x00\x00\x87"
  "\x00\x00\x00\x85\x80\x00\x00\x86\xc0\x40\x01\x9c\x80\x80\x00\xc0\x00\x80"
  "\x00\x05\x01\x01\x00\xdc\x80\x00\x01\x05\x81\x00\x00\x06\xc1\x40\x02\x1c"
  "\x81\x80\x00\x0d\x81\x00\x02\x45\x41\x01\x00\x80\x01\x00\x00\xc5\x01\x01"
  "\x00\x00\x02\x80\x01\x40\x02\x00\x02\x85\x02\x00\x00\x5c\x41\x00\x03\x1e"
  "\x00\x80\x00\x06\x00\x00\x00\x04\x02\x00\x00\x00\x00\x00\x00\x00\x4e\x00"
  "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x04\x03\x00\x00\x00\x00\x00\x00\x00"
  "\x6f\x73\x00\x04\x06\x00\x00\x00\x00\x00\x00\x00\x63\x6c\x6f\x63\x6b\x00"
  "\x04\x02\x00\x00\x00\x00\x00\x00\x00\x6e\x00\x04\x06\x00\x00\x00\x00\x00"
  "\x00\x00\x70\x72\x69\x6e\x74\x00\x00\x00\x00\x00\x14\x00\x00\x00\x1c\x00"
  "\x00\x00\x1c\x00\x00\x00\x1d\x00\x00\x00\x1d\x00\x00\x00\x1d\x00\x00\x00"
  "\x1e\x00\x00\x00\x1e\x00\x00\x00\x1e\x00\x00\x00\x1f\x00\x00\x00\x1f\x00"
  "\x00\x00\x1f\x00\x00\x00\x1f\x00\x00\x00\x20\x00\x00\x00\x20\x00\x00\x00"
  "\x20\x00\x00\x00\x20\x00\x00\x00\x20\x00\x00\x00\x20\x00\x00\x00\x20\x00"
  "\x00\x00\x21\x00\x00\x00\x05\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00"
  "\x73\x00\x00\x00\x00\x00\x13\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00"
  "\x66\x00\x00\x00\x00\x00\x13\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00"
  "\x63\x00\x05\x00\x00\x00\x13\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00"
  "\x76\x00\x08\x00\x00\x00\x13\x00\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00"
  "\x74\x00\x0c\x00\x00\x00\x13\x00\x00\x00\x00\x00\x00\x00\x24\x00\x00\x00"
  "\x0b\x00\x00\x00\x04\x00\x00\x00\x18\x00\x00\x00\x0e\x00\x00\x00\x21\x00"
  "\x00\x00\x1b\x00\x00\x00\x23\x00\x00\x00\x23\x00\x00\x00\x23\x00\x00\x00"
  "\x23\x00\x00\x00\x23\x00\x00\x00\x23\x00\x00\x00\x24\x00\x00\x00\x24\x00"
  "\x00\x00\x24\x00\x00\x00\x24\x00\x00\x00\x25\x00\x00\x00\x25\x00\x00\x00"
  "\x25\x00\x00\x00\x25\x00\x00\x00\x25\x00\x00\x00\x25\x00\x00\x00\x25\x00"
  "\x00\x00\x26\x00\x00\x00\x26\x00\x00\x00\x26\x00\x00\x00\x26\x00\x00\x00"
  "\x27\x00\x00\x00\x27\x00\x00\x00\x27\x00\x00\x00\x27\x00\x00\x00\x28\x00"
  "\x00\x00\x28\x00\x00\x00\x28\x00\x00\x00\x28\x00\x00\x00\x28\x00\x00\x00"
  "\x00\x00\x00\x00\x00\x00\x00\x00"
;

int main() {
  /* Right now, mainly a sanity check that parsing actually works */
  luac_file_t file;
  luac_parse(&file, to_parse, SRC_UNKNOWN, "<data>");

  /* TODO: actually verify parsed entries */

  luac_close(&file);
  return 0;
}
