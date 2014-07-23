write('init started\n');
write('init done\n');

exec({print: print}, 'keyboard.js', load('/keyboard.js'))

// var p
// while(true) {
//   if(poll()) {
//     write(key(inb(0x60)))
//   }
// }
