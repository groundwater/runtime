var Screen = require('./screen.js')
var map = require('./keymap.js')

var start = 0xB8000
var bytes = 2
var cols  = 80
var rows  = 25
var size  = cols * rows * bytes

var display = new Uint16Array(buff(start, size))
var screen  = new Screen(display)

screen.write('Welcome to Runtime :)')

function prompt() {
  screen.newline()
  screen.write(' >')
}

prompt()

while(true) {
  var num
    , key

  if (poll())
  if (num = inb(0x60))
  if (key = map(num))
  if (key === '\n') prompt()
  else if (key === '\b') screen.backspace()
  else if (key) screen.write(key)
  else screen.write('.')

}
