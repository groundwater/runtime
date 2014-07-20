// this needs the print global to exist
// make sure your bootstrapping code passes it in

setImmediate(function(){
  print("+immediate\n")
})

print("-immediate\n")
