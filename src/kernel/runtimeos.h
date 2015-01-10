#include <v8.h>

namespace RuntimeOS {

  using namespace v8;
  using std::vector;

  //-------------------------------------------
  // GLOBALS - because everyone loves globals
  //-------------------------------------------

  // cpu "ticks"
  uint64_t ticks = 0;

  // IRQ events
  vector<uint64_t> queue;

  //--------------------//
  // INTERRUPT HANDLERS //
  //--------------------//

  extern "C" void irq_handler_any(uint64_t number) {
      queue.push_back(number);

      // https://github.com/runtimejs/runtime/blob/master/initrd/system/driver/ps2kbd.js#L155
      *(volatile uint32_t*)(0xfee00000 + 0x00b0) = 0;
  };

  // run this interrupt every time a cpu tick occurs
  extern "C" void irq_timer_event() {
      ticks++;

      *(volatile uint32_t*)(0xfee00000 + 0x00b0) = 0;
  };

  Handle<ObjectTemplate> MakeGlobal(Isolate *isolate);

  void Eval(const FunctionCallbackInfo<Value>& args) {
    Handle<String> code = args[0]->ToString();
    Handle<String> file = args[1]->ToString();

    Handle<Script> script = Script::Compile(code, file);

    // run script
    Handle<Value> ret = script->Run();

    args.GetReturnValue().Set(ret);
  };

  // create an array buffer mapped by arbitrary memory
  // you can do real damage with this bad boy
  void Buffer(const FunctionCallbackInfo<Value>& args) {
    uint64_t base = args[0]->ToNumber()->Value();
    uint64_t size = args[1]->ToNumber()->Value();

    Handle<ArrayBuffer> buff = ArrayBuffer::New(
      args.GetIsolate(),
      reinterpret_cast<void*>(base), // base address of buffer
      size // offset
    );

    args.GetReturnValue().Set(buff);
  };

  void InByte(const FunctionCallbackInfo<Value>& args) {
    uint64_t port = args[0]->ToNumber()->Value();
    uint8_t value;

    // read a byte from the specified I/O port
    asm volatile("inb %w1, %b0": "=a"(value): "d"(port));

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), value));
  };

  void OutByte(const FunctionCallbackInfo<Value>& args) {
    uint16_t port = args[0]->ToNumber()->Value();
    uint8_t val = args[1]->ToNumber()->Value();

    // write a byte to the specified I/O port
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), 0));
  };

  // poll for queued interrupts
  void Poll(const FunctionCallbackInfo<Value>& args) {
    if (queue.size() > 0) {
      // there is an event in the queue
      uint64_t e = queue.back();
      args.GetReturnValue().Set(Number::New(args.GetIsolate(), e));
      queue.pop_back();
    } else {
      // the event queue is empty
      args.GetReturnValue().SetUndefined();
    }
  };

  // get number of ticks since CPU started
  // this can be used to measure real time
  void Ticks(const FunctionCallbackInfo<Value>& args) {
    args
      .GetReturnValue()
      .Set(Number::New(args.GetIsolate(), ticks));
  };

  // this is the public kernel API
  Handle<ObjectTemplate> MakeGlobal(Isolate *isolate) {
    Handle<ObjectTemplate> global = ObjectTemplate::New(isolate);

    global->Set(String::NewFromUtf8(isolate, "ticks"),
                FunctionTemplate::New(isolate, Ticks));

    global->Set(String::NewFromUtf8(isolate, "eval"),
                FunctionTemplate::New(isolate, Eval));

    global->Set(String::NewFromUtf8(isolate, "poll"),
                FunctionTemplate::New(isolate, Poll));

    global->Set(String::NewFromUtf8(isolate, "inb"),
                FunctionTemplate::New(isolate, InByte));

    global->Set(String::NewFromUtf8(isolate, "outb"),
                FunctionTemplate::New(isolate, OutByte));

    global->Set(String::NewFromUtf8(isolate, "buff"),
                FunctionTemplate::New(isolate, Buffer));

    return global;
  };

  void Main(char* str) {
    Isolate* isolate = Isolate::New();

    // v8 boilerplate
    Locker locker(isolate);
    Isolate::Scope isolateScope(isolate);
    HandleScope handleScope(isolate);

    // populate global object with C++ wrapped functions
    Handle<ObjectTemplate> global = MakeGlobal(isolate);
    Handle<Context> context = Context::New(isolate, NULL, global);

    Context::Scope contextScope(context);

    // compile the script from the initrd file
    Handle<String> file = String::NewFromUtf8(isolate, "system.js");
    Handle<String> code = String::NewFromUtf8(isolate, str);

    // Handle<String> code = String::
    Handle<Script> script = Script::Compile(code, file);

    // run script
    script->Run();
    isolate->Dispose();
  };
}
