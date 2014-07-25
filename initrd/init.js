// print to console
print("This better work\n")

var ctx = {
  print: print
}
var key = exec(ctx, 'keyboard.js', load('/keyboard.js'))

var p
while(true) {
  if(p = poll()) {
    print(key(inb(0x60))||'')
  }
}
