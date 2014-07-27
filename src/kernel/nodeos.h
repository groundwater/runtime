#include <v8.h>
#include <kernel/initrd.h>
#include <kernel/platform.h>

namespace RuntimeNodeOS {

  using namespace v8;
  using rt::InitrdFile;
  using rt::LocalApicRegisterAccessor;
  using rt::LocalApicRegister;

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

  static void GlobalPropertyGetterCallback(Local<String> property, const PropertyCallbackInfo<Value>& args) {

    Isolate* isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Handle<Object> obj = args.Data()->ToObject();

    args.GetReturnValue().Set(obj->Get(property));
  };

  static void Eval(const FunctionCallbackInfo<Value>& args) {
    Handle<String> code = args[0]->ToString();
    Handle<String> file = args[1]->ToString();

    Handle<Script> script = Script::Compile(code, file);

    // run script
    Handle<Value> ret = script->Run();

    args.GetReturnValue().Set(ret);
  };

  static void MakeContext(const FunctionCallbackInfo<Value>& args) {

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

  static void CreateIsolate(const FunctionCallbackInfo<Value>& args) {
    Isolate* isolate = Isolate::New();

    Handle<External> ext = External::New(args.GetIsolate(), isolate);

    args.GetReturnValue().Set(ext);
  };

  static void CreateContext(const FunctionCallbackInfo<Value>& args) {

    Isolate *iso = reinterpret_cast<Isolate *>(Handle<External>::Cast(args[0])->Value());

    Handle<Context> ctx = Context::New(iso);


  };

  static void MakeIsolate(const FunctionCallbackInfo<Value>& args) {

    Handle<String> x = args[0]->ToString();
    int l = x->Length();
    uint16_t buff[l];
    x->Write(buff, 0, l);

    // create a new isolate
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
          Handle<String> code = String::NewFromTwoByte(isolate, buff);
          Handle<Script> script = Script::Compile(code, file);

          // run script
          Handle<Value> ret = script->Run();
          String::Utf8Value val(ret->ToString());

          // printf("Exit Main: %s\n", *val);
      }

    }

    isolate->Dispose();

  };

  // this is our event queue
  static std::vector<uint64_t> queue;

  static void Buffer(const FunctionCallbackInfo<Value>& args) {

    uint64_t base = args[0]->ToNumber()->Value();
    uint64_t size = args[1]->ToNumber()->Value();

    Handle<ArrayBuffer> buff = ArrayBuffer::New(
      args.GetIsolate(),
      reinterpret_cast<void*>(base),
      size
    );

    args.GetReturnValue().Set(buff);
  };

  static void InByte(const FunctionCallbackInfo<Value>& args) {
    uint64_t port = args[0]->ToNumber()->Value();
    uint8_t value;

    // read a byte from the specified I/O port
    asm volatile("inb %w1, %b0": "=a"(value): "d"(port));

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), value));
  };

  static void Poll(const FunctionCallbackInfo<Value>& args) {
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

  // JACOB: this runs in the interrupt handler
  extern "C" void irq_handler_any(uint64_t number) {
      queue.push_back(number);

      // https://github.com/runtimejs/runtime/blob/master/initrd/system/driver/ps2kbd.js#L155
      GLOBAL_platform()->ackIRQ();
  };

  static uint64_t ticks = 0;

  extern "C" void irq_timer_event() {
      ticks++;

      LocalApicRegisterAccessor registers((void*)0xfee00000);
      registers.Write(LocalApicRegister::EOI, 0);
  };

  static void Ticks(const FunctionCallbackInfo<Value>& args) {

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
                FunctionTemplate::New(isolate, MakeContext));

    global->Set(String::NewFromUtf8(isolate, "poll"),
                FunctionTemplate::New(isolate, Poll));

    global->Set(String::NewFromUtf8(isolate, "inb"),
                FunctionTemplate::New(isolate, InByte));

    global->Set(String::NewFromUtf8(isolate, "buff"),
                FunctionTemplate::New(isolate, Buffer));

    return global;
  };

  void Main(uint8_t* place) {
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
            Handle<String> code = String::NewFromOneByte(isolate, place);
            Handle<Script> script = Script::Compile(code, file);

            // run script
            Handle<Value> ret = script->Run();
            String::Utf8Value val(ret->ToString());

            printf("Exit Main: %s\n", *val);
        }

    }

    isolate->Dispose();

  };

}
