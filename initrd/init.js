// print to console
// print("This better work\n")

// iso(load('/iso.js'))

var ctx = {
  // print: print
}
var key = exec(ctx, 'keyboard.js', load('/keyboard.js'))
var cr3 = reg(13)

// print('cr3: ' + cr3 + '\n')
// print('rax: ' + reg(50) + '\n')

// display 0x000A0000
// length 0x1FFFF

// var start = 0x18000 + 0x000A0000
var start = 0xB8000
var bytes = 2
var cols  = 80
var rows  = 25
var size  = cols * rows * bytes

var bu = buff(start, size)
var b  = new Uint16Array(bu)

for(var i=0; i<b.length; i++){
  b[i] = 0
}


// http://wiki.osdev.org/Text_UI
var header = [
"  _____             _   _                ",
" |  __ \\           | | (_)               ",
" | |__) |   _ _ __ | |_ _ _ __ ___   ___ ",
" |  _  / | | | '_ \\| __| | '_ ` _ \\ / _ \\",
" | | \\ \\ |_| | | | | |_| | | | | | |  __/",
" |_|  \\_\\__,_|_| |_|\\__|_|_| |_| |_|\\___|"
]

var y = 0
var hx = 0x0C << 8

header.forEach(function(line){
  for(var i=0; i<line.length; i++)
    b[i + y*80] = hx | line.charCodeAt(i)
  y++
})

function Screen(buffer){
  this.buffer = buffer
  this.bytes  = 2
  this.cols   = 80
  this.rows   = 25
  this.color  = 0x0A
  //             row, col
  this.cursor = [ 0 ,  0 ]
}

Screen.prototype.nextChar = function() {
  var row = this.cursor[0]
  var col = this.cursor[1]

  if (row === this.rows) {
    this.cursor[0] = row + 1
    this.cursor[1] = 0
  } else {
    this.cursor[1] = col + 1
  }
}

Screen.prototype.linearChar = function () {
  return this.cursor[0] * this.cols + this.cursor[1]
}

Screen.prototype.returnChar = function () {
  this.cursor[0]++
  this.cursor[1] = 0
}

Screen.prototype.writeChar = function (c) {
  var pos = this.linearChar()
  this.buffer[pos] = this.color << 8 | c.charCodeAt(0)
  this.nextChar()
}

Screen.prototype.write = function (line) {
  for(var i=0; i<line.length; i++) {
    var char = line[i]
    if (char === '\n') {
      this.returnChar()
    } else {
      this.writeChar(char)
    }
  }
}

var screen = new Screen(b)

screen.cursor = [10, 0]

var i = 0
function prompt() {
  screen.returnChar()
  screen.write("runtime > ")
}

screen.write("Welcome to Runtime")
prompt()

var p
while(true) {
  if(p = poll()) {
    var c = key(inb(0x60))
    if (c === '\n') {
      screen.write("\n--> OKAY!")
      prompt()
    } else if (c) {
      screen.write(c)
    }
  }
}
