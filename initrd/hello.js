// print to console
print("This better work\n")

// load a file from initrd
var hello = load("/hello.txt")

// totally unguarded, make sure hello exists :)
print(hello)

// event queue
var events = []
var setImmediate = function(callback) {
  events.push(callback)
}

// create a new global context object to pass ot a "child" context
var context = {
  print        : print,
  setImmediate : setImmediate,
}
var filename = "/goodbye.js"
var filedata = load(filename)

// execute a new context on the next tick
setImmediate(function(){
  exec(context, filename, filedata)
})

var keymapNormal = [
    '', '', '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', '', 'a', 's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`', '', '\\', 'z', 'x', 'c', 'v',
    'b', 'n', 'm', ',', '.', '/', '', '', '', ' ', '', '', '', '', '', '', '', '', '', '', '', '', '', '',
    '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', '', ''
];

var keymap = function(code){
  // ignore up event
  if (code & 0x80) return '';

  var character = '?';
  code &= 0x7F;
  character = keymapNormal[code];

  return character
}

var buffer = []
var input = function(char){
  if (char==='\n') {
    print('\n')
    try {
      print(exec({}, '<REPL>', buffer.join('')))
    } catch(e) {
      print(e)
    }
    buffer = []
    print('\n')
  } else {
    buffer.push(char)
    print(char)
  }
}

// uvrun :)
var tick
while(tick = events.shift()) {
  tick()

  events.push(function(){
    var key = poll()
    if (key) input(keymap(key))
  })
}

// this will not occur until above context exist
print("done\n")
