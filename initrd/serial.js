var PORT = 0x3f8;
var writeBuffer = [];
var canWrite = false;
var canRead = false;
var readCallback;
var screen;

function iir() {
    return inb(0x3FA);
}

function msr() {
    return inb(0x3FE);
}


function lsr() {
  return inb(0x3FD);
}

//Helpful links:
//  http://www.lammertbies.nl/comm/info/serial-uart.html#FCR
//  http://flint.cs.yale.edu/cs422/doc/art-of-asm/pdf/CH22.PDF
//  http://wiki.osdev.org/PIC
function handleInt() {
  var register = iir();
  var hasInterrupt = register & 1;

  switch(register & 0xE) {
      case 0: //Modem status change
        msr();
        break;

      case 2: //Transmitter holding register empty - ready to send
        canWrite = true;
        if (writeBuffer.length === 0) {
          iir();
        } else {
          var c = writeBuffer.shift();
          outb(0x3f8, c);
        }
        break;

      case 4: //Received data available - ready to read
        readCallback(inb(0x3f8));
        break;

      case 6: //Line status change
        var status = lsr();
        if (status & 1 === 1) {
          readCallback(inb(0x3f8));
        }
        break;

      default:
        break;
  }
}


module.exports = {
  init: function(scr, cb) {
    screen = scr;
    readCallback = cb;

    outb(0x70, inb(0x70) | 0x80);  //disable NMI

    outb(0x20, 0x11); // 00010001b, begin PIC 1 initialization
    outb(0xA0, 0x11); // 00010001b, begin PIC 2 initialization

    outb(0x21, 0x40); // IRQ 0-7, interrupts 40h-47h
    outb(0xA1, 0x48); // IRQ 8-15, interrupts 48h-4Fh

    outb(0x21, 0x04);
    outb(0xA1, 0x02);

    outb(0x21, 0x01);
    outb(0xA1, 0x01);

    // Mask all PIC interrupts
    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    outb(PORT + 1, 0x00);    // Disable all interrupts
    outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(PORT + 0, 0x01);    // Set divisor to 1 (lo byte) 56K baud
    outb(PORT + 1, 0x00);    //                  (hi byte)
    outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(PORT + 2, 0x03);    // Enable FIFO, clear them, with 1-byte threshold
    outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set

    outb(PORT + 1, 0x0F);    // Enable all interrupts for IRQ 1 - 16
  },

  write: function(str) {
    for(var i = 0; i < str.length; i++) {
      writeBuffer.push(str.charCodeAt(i));
    }

    if(writeBuffer.length) {
      outb(0x3f8, writeBuffer.shift());
    }
  },
  handleInt: handleInt
};