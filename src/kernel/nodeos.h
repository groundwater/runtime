#include <v8.h>
#include <kernel/initrd.h>

namespace RuntimeNodeOS {

  using namespace v8;
  using rt::InitrdFile;
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

  extern "C" void irq_timer_event() {
      ticks++;

      *(volatile uint32_t*)(0xfee00000 + 0x00b0) = 0;
  };

  Handle<ObjectTemplate> MakeGlobal(Isolate *isolate);

  void InitrdLoad(const FunctionCallbackInfo<Value>& args) {
    HandleScope scope(args.GetIsolate());
    String::Utf8Value val(args[0]->ToString());

    InitrdFile startup_file = GLOBAL_initrd()->Get(*val);
    size_t size = startup_file.Size();
    const uint8_t* data = startup_file.Data();

    uint8_t place[size + 1];
    place[size] = '\0';
    memcpy(place, data, size);

    Handle<String> file = String::NewFromOneByte(args.GetIsolate(), place);

    args.GetReturnValue().Set(file);
  };

  // callback used to initialize the global object in a new context
  void GlobalPropertyGetterCallback(Local<String> property, const PropertyCallbackInfo<Value>& args) {
    Isolate* isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Handle<Object> obj = args.Data()->ToObject();

    args.GetReturnValue().Set(obj->Get(property));
  };

  void Eval(const FunctionCallbackInfo<Value>& args) {
    Handle<String> code = args[0]->ToString();
    Handle<String> file = args[1]->ToString();

    Handle<Script> script = Script::Compile(code, file);

    // run script
    Handle<Value> ret = script->Run();

    args.GetReturnValue().Set(ret);
  };

  // run in a new context, passing in whatever you like to the global object
  void RunInNewContext(const FunctionCallbackInfo<Value>& args) {

    Isolate* isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Handle<Object> obj = args[0]->ToObject();
    Handle<ObjectTemplate> global = ObjectTemplate::New(isolate);

    global->SetNamedPropertyHandler(GlobalPropertyGetterCallback,0,0,0,0,obj);

    // run the context
    Handle<Context> context = Context::New(isolate, NULL, global);
    Handle<Array> names = obj->GetOwnPropertyNames();
    for(uint32_t i=0; i<names->Length(); i++)
    {
      String::Utf8Value sstr(names->Get(i)->ToString());
      global->Set(names->Get(i)->ToString(), obj->Get(names->Get(i)));
    }

    Handle<String> str = args[1]->ToString();

    {
      Context::Scope contextScope(context);

      Handle<String> file = args[1]->ToString();
      Handle<String> code = args[2]->ToString();
      Handle<Script> script = Script::Compile(code, file);

      // run script
      Handle<Value> ret = script->Run();

      args.GetReturnValue().Set(ret);
    }

  };

  void RunInNewIsolate(const FunctionCallbackInfo<Value>& args) {
    Handle<String> code = args[0]->ToString();

    Isolate* isolate = Isolate::New();
    {
      Locker locker(isolate);

      Isolate::Scope isolateScope(isolate);
      HandleScope handleScope(isolate);

      // create a global object templates to pass into the context
      Handle<ObjectTemplate> global = MakeGlobal(isolate);

      Handle<Context> context = Context::New(isolate, NULL, global);
      {
          // setup the context
          Context::Scope contextScope(context);

          // compile the script from the initrd file
          Handle<String> file = String::NewFromUtf8(isolate, "init.js");
          Handle<Script> script = Script::Compile(code, file);

          // run script
          Handle<Value> ret = script->Run();
          String::Utf8Value val(ret->ToString());
      }

    }

    isolate->Dispose();
  };

  // create an array buffer mapped by arbitrary memory
  // you can do real damage with this bad boy
  void Buffer(const FunctionCallbackInfo<Value>& args) {
    uint64_t base = args[0]->ToNumber()->Value();
    uint64_t size = args[1]->ToNumber()->Value();

    Handle<ArrayBuffer> buff = ArrayBuffer::New(
      args.GetIsolate(),
      reinterpret_cast<void*>(base),
      size
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

    global->Set(String::NewFromUtf8(isolate, "load"),
                FunctionTemplate::New(isolate, InitrdLoad));

    global->Set(String::NewFromUtf8(isolate, "exec"),
                FunctionTemplate::New(isolate, RunInNewContext));

    global->Set(String::NewFromUtf8(isolate, "poll"),
                FunctionTemplate::New(isolate, Poll));

    global->Set(String::NewFromUtf8(isolate, "inb"),
                FunctionTemplate::New(isolate, InByte));

    global->Set(String::NewFromUtf8(isolate, "buff"),
                FunctionTemplate::New(isolate, Buffer));

    return global;
  };

  class ExternalInitrd : public String::ExternalStringResource {
  public:
    ExternalInitrd(const uint16_t* data, const size_t length):
      _data(data),
      _length(length)
      {}
    const uint16_t* data() const {
      return _data;
    }
    size_t length() const {
      return _length;
    }
  private:
    const uint16_t* _data;
    const size_t _length;
  };

  void Main(const uint16_t* start, const size_t length) {
    Isolate* isolate = Isolate::New();
    {

        // v8 boilerplate
        Locker locker(isolate);
        Isolate::Scope isolateScope(isolate);
        HandleScope handleScope(isolate);

        // populate global object with C++ wrapped functions
        Handle<ObjectTemplate> global = MakeGlobal(isolate);
        Handle<Context> context = Context::New(isolate, NULL, global);
        {
            Context::Scope contextScope(context);

            // compile the script from the initrd file
            Handle<String> file = String::NewFromUtf8(isolate, "init.js");
            // Handle<String> code = String::NewFromOneByte(isolate, init);
            ExternalInitrd ext(start, length);
            Handle<String> code = String::NewExternal(isolate, &ext);

            // Handle<String> code = String::
            Handle<Script> script = Script::Compile(code, file);

            // run script
            Handle<Value> ret = script->Run();
            String::Utf8Value val(ret->ToString());

           // printf("Exit Main: %s\n", *val);
        }

    }
    isolate->Dispose();

  };

}
