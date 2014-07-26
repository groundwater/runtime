// print to console
print("This better work\n")

iso(load('/iso.js'))

var ctx = {
  print: print
}
var key = exec(ctx, 'keyboard.js', load('/keyboard.js'))
var cr3 = reg(13)

print('cr3: ' + cr3 + '\n')
print('rax: ' + reg(50) + '\n')

// display 0x000A0000
// length 0x1FFFF

// var start = 0x18000 + 0x000A0000
// var cols  = 160
// var rows  = 25
// var size  = cols * rows
//
// var bu = buff(start, size)
// var b  = new Uint8Array(bu)
//
// for(i = 0; i<b.length; i++) {
//   // if (i % 16 === 0) print('\n')
//   // var st = b[i].toString(16)
//   // if(st.length === 1) st += '0'
//   // print(st + ' ')
//   b[i] = 0xaa // green
//   b[i] = 0xbb // blueish
//   b[i] = 0xee // yellow
//   b[i] = 0xcc // red
//   b[i] = 0xdd // pink
//   b[i] = 0x55 // purple
//   b[i] = 0x44 // dark red
// }

var p
while(true) {
  if(p = poll()) {
    print(key(inb(0x60))||'')
  }
}
