// print to console
print("This better work")

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

// uvrun :)
var tick
while(tick = events.shift()) {
  tick()
}

// this will not occur until above context exist
print("done")
