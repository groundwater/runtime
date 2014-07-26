(function(){

  function Util(){}

  Util.prototype.hexdump = function(bu){
    var b  = new Uint8Array(bu)

    for(i = 0; i<b.length; i++) {
      if (i % 16 === 0) print('\n')
      var st = b[i].toString(16)
      if(st.length === 1) st += '0'
      print(st + ' ')
    }
  }

  return new Util();

})()
