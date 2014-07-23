// print to console
print("This better work")

var ctx = {
  print: print
}
var key = exec(ctx, 'keyboard.js', load('/keyboard.js'))

var p
while(true) {
  if(p = poll()) {
    print(key(p)||'')
  }
}
