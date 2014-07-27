#include <v8.h>
#include <kernel/initrd.h>
#include <kernel/platform.h>

namespace RuntimeNodeOS {

  using namespace v8;

  v8::Handle<v8::ObjectTemplate> MakeGlobal(v8::Isolate *isolate);

  void InitrdLoad(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::HandleScope scope(args.GetIsolate());
    v8::String::Utf8Value val(args[0]->ToString());

    rt::InitrdFile startup_file = GLOBAL_initrd()->Get(*val);
    size_t size = startup_file.Size();
    const uint8_t* data = startup_file.Data();

    uint8_t place[size + 1];
    place[size] = '\0';
    memcpy(place, data, size);

    v8::Handle<v8::String> file = v8::String::NewFromOneByte(args.GetIsolate(), place);

    args.GetReturnValue().Set(file);
  };

  static void GlobalPropertyGetterCallback(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
    using namespace v8;

    v8::Isolate* isolate = args.GetIsolate();
    v8::HandleScope scope(isolate);

    Handle<Object> obj = args.Data()->ToObject();

    args.GetReturnValue().Set(obj->Get(property));
  };

  static void Eval(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Handle<v8::String> code = args[0]->ToString();
    v8::Handle<v8::String> file = args[1]->ToString();

    v8::Handle<v8::Script> script = v8::Script::Compile(code, file);

    // run script
    v8::Handle<v8::Value> ret = script->Run();

    args.GetReturnValue().Set(ret);
  };

  static void MakeContext(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;

    v8::Isolate* isolate = args.GetIsolate();
    HandleScope scope(isolate);

    Handle<Object> obj = args[0]->ToObject();
    Handle<ObjectTemplate> global = ObjectTemplate::New(isolate);

    global->SetNamedPropertyHandler(GlobalPropertyGetterCallback,0,0,0,0,obj);

    // run the context
    Handle<Context> context = Context::New(isolate, NULL, global);
    Handle<Array> names = obj->GetOwnPropertyNames();
    for(uint32_t i=0; i<names->Length(); i++)
    {
      v8::String::Utf8Value sstr(names->Get(i)->ToString());
      global->Set(names->Get(i)->ToString(), obj->Get(names->Get(i)));
    }

    v8::Handle<v8::String> str = args[1]->ToString();

    {
      v8::Context::Scope contextScope(context);

      v8::Handle<v8::String> file = args[1]->ToString();
      v8::Handle<v8::String> code = args[2]->ToString();
      v8::Handle<v8::Script> script = v8::Script::Compile(code, file);

      // run script
      v8::Handle<v8::Value> ret = script->Run();

      args.GetReturnValue().Set(ret);
    }

  };

  static void CreateIsolate(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = v8::Isolate::New();

    v8::Handle<v8::External> ext = v8::External::New(args.GetIsolate(), isolate);

    args.GetReturnValue().Set(ext);
  };

  static void CreateContext(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;

    v8::Isolate *iso = reinterpret_cast<v8::Isolate *>(Handle<External>::Cast(args[0])->Value());

    Handle<Context> ctx = Context::New(iso);


  };

  static void MakeIsolate(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;

    v8::Handle<v8::String> x = args[0]->ToString();
    int l = x->Length();
    uint16_t buff[l];
    x->Write(buff, 0, l);

    // create a new isolate
    v8::Isolate* isolate = v8::Isolate::New();
    {
      v8::Locker locker(isolate);

      v8::Isolate::Scope isolateScope(isolate);
      v8::HandleScope handleScope(isolate);

      // create a global object templates to pass into the context
      v8::Handle<v8::ObjectTemplate> global = MakeGlobal(isolate);

      v8::Handle<v8::Context> context = v8::Context::New(isolate, NULL, global);
      {
          // setup the context
          v8::Context::Scope contextScope(context);

          // compile the script from the initrd file
          v8::Handle<v8::String> file = v8::String::NewFromUtf8(isolate, "init.js");
          v8::Handle<v8::String> code = v8::String::NewFromTwoByte(isolate, buff);
          v8::Handle<v8::Script> script = v8::Script::Compile(code, file);

          // run script
          v8::Handle<v8::Value> ret = script->Run();
          v8::String::Utf8Value val(ret->ToString());

          // printf("Exit Main: %s\n", *val);
      }

    }

    isolate->Dispose();

  };

  // this is our event queue
  static std::vector<uint64_t> queue;

  static void Buffer(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;

    uint64_t base = args[0]->ToNumber()->Value();
    uint64_t size = args[1]->ToNumber()->Value();

    Handle<ArrayBuffer> buff = ArrayBuffer::New(
      args.GetIsolate(),
      reinterpret_cast<void*>(base),
      size
    );

    args.GetReturnValue().Set(buff);
  };

  static void InByte(const v8::FunctionCallbackInfo<v8::Value>& args) {
    uint64_t port = args[0]->ToNumber()->Value();
    uint8_t value;

    // read a byte from the specified I/O port
    asm volatile("inb %w1, %b0": "=a"(value): "d"(port));

    args.GetReturnValue().Set(v8::Number::New(args.GetIsolate(), value));
  };

  static void Poll(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;
    if (queue.size() > 0) {
      // there is an event in the queue
      uint64_t e = queue.back();
      args.GetReturnValue().Set(v8::Number::New(args.GetIsolate(), e));
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

      rt::LocalApicRegisterAccessor registers((void*)0xfee00000);
      registers.Write(rt::LocalApicRegister::EOI, 0);
  };

  static void Ticks(const v8::FunctionCallbackInfo<v8::Value>& args) {
    using namespace v8;

    args
      .GetReturnValue()
      .Set(Number::New(args.GetIsolate(), ticks));
  };

  // this is the public kernel API
  v8::Handle<v8::ObjectTemplate> MakeGlobal(v8::Isolate *isolate) {
    v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

    global->Set(v8::String::NewFromUtf8(isolate, "ticks"),
                v8::FunctionTemplate::New(isolate, Ticks));

    global->Set(v8::String::NewFromUtf8(isolate, "eval"),
                v8::FunctionTemplate::New(isolate, Eval));

    global->Set(v8::String::NewFromUtf8(isolate, "load"),
                v8::FunctionTemplate::New(isolate, InitrdLoad));

    global->Set(v8::String::NewFromUtf8(isolate, "exec"),
                v8::FunctionTemplate::New(isolate, MakeContext));

    global->Set(v8::String::NewFromUtf8(isolate, "poll"),
                v8::FunctionTemplate::New(isolate, Poll));

    global->Set(v8::String::NewFromUtf8(isolate, "inb"),
                v8::FunctionTemplate::New(isolate, InByte));

    global->Set(v8::String::NewFromUtf8(isolate, "buff"),
                v8::FunctionTemplate::New(isolate, Buffer));

    return global;
  };

  void Main(uint8_t* place) {
    v8::Isolate* isolate = v8::Isolate::New();
    {
        using namespace v8;

        v8::Locker locker(isolate);

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        // create a global object templates to pass into the context
        v8::Handle<v8::ObjectTemplate> global = MakeGlobal(isolate);

        v8::Handle<v8::Context> context = v8::Context::New(isolate, NULL, global);
        {
            // setup the context
            v8::Context::Scope contextScope(context);

            // compile the script from the initrd file
            v8::Handle<v8::String> file = v8::String::NewFromUtf8(isolate, "init.js");
            v8::Handle<v8::String> code = v8::String::NewFromOneByte(isolate, place);
            v8::Handle<v8::Script> script = v8::Script::Compile(code, file);

            // run script
            v8::Handle<v8::Value> ret = script->Run();
            v8::String::Utf8Value val(ret->ToString());

            printf("Exit Main: %s\n", *val);
        }

    }

    isolate->Dispose();

  };

}
