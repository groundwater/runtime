// this needs the print global to exist
// make sure your bootstrapping code passes it in

setImmediate(function(){
  print("+immediate")
})

print("-immediate")
