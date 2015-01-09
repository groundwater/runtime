#include <v8.h>
#include <kernel/initrd.h>

void ReportException(v8::Isolate* isolate, v8::TryCatch* handler);

const char* ToCString(const v8::String::Utf8Value& value) {
  return *value ? *value : "<string conversion failed>";
}

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

  typedef struct SMAP_entry {
    uint32_t BaseL; // base address QWORD
    uint32_t BaseH;
    uint32_t LengthL; // length QWORD
    uint32_t LengthH;
    uint32_t Type; // entry Type
    uint32_t ACPI; // extended
  }__attribute__((packed)) SMAP_entry_t;



  Handle<ObjectTemplate> MakeGlobal(Isolate *isolate);

  void ReportException(v8::Isolate* isolate, v8::TryCatch* try_catch) {
    v8::HandleScope handle_scope(isolate);
    v8::String::Utf8Value exception(try_catch->Exception());
    const char* exception_string = ToCString(exception);
    v8::Handle<v8::Message> message = try_catch->Message();
    if (message.IsEmpty()) {
      // V8 didn't provide any extra information about this error; just
      // print the exception.
      fprintf(stderr, "%s\n", exception_string);
    } else {
      // Print (filename):(line number): (message).
      v8::String::Utf8Value filename(message->GetScriptOrigin().ResourceName());
      const char* filename_string = ToCString(filename);
      int linenum = message->GetLineNumber();
      fprintf(stderr, "%s:%i: %s\n", filename_string, linenum, exception_string);
      // Print line of source code.
      v8::String::Utf8Value sourceline(message->GetSourceLine());
      const char* sourceline_string = ToCString(sourceline);
      fprintf(stderr, "%s\n", sourceline_string);
      // Print wavy underline (GetUnderline is deprecated).
      int start = message->GetStartColumn();
      for (int i = 0; i < start; i++) {
        fprintf(stderr, " ");
      }
      int end = message->GetEndColumn();
      for (int i = start; i < end; i++) {
        fprintf(stderr, "^");
      }
      fprintf(stderr, "\n");
      v8::String::Utf8Value stack_trace(try_catch->StackTrace());
      if (stack_trace.length() > 0) {
        const char* stack_trace_string = ToCString(stack_trace);
        fprintf(stderr, "%s\n", stack_trace_string);
      }
    }
  }

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

  void Print(const FunctionCallbackInfo<Value>& args) {
    String::Utf8Value x(args[0]->ToString());
    printf("%s", *x);
  }

  void InByte(const FunctionCallbackInfo<Value>& args) {
    uint64_t port = args[0]->ToNumber()->Value();
    uint8_t value;

    // read a byte from the specified I/O port
    asm volatile("inb %w1, %b0": "=a"(value): "d"(port));

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), value));
  };

  void OutByte(const FunctionCallbackInfo<Value>& args) {
    uint64_t port = args[0]->ToNumber()->Value();
    uint64_t val = args[1]->ToNumber()->Value();
    uint8_t value;

    // write a byte to the specified I/O port
    asm volatile ( "outb %0, %1" : : "a"(val), "Nd"(port) );

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), value));
  };

  void ReadMem(const FunctionCallbackInfo<Value>& args) {
    SMAP_entry_t* buffer = (SMAP_entry_t*) 0x1000;
    const int smap_size = 0x2000;

    int maxentries = smap_size / sizeof(buffer);
 
    int tick = 0;

    uint32_t contID = 0;
    int entries = 0, signature, bytes;
    do 
    {
      asm volatile ("int $0x15" 
          : "=a"(signature), "=c"(bytes), "=b"(contID)
          : "a"(0xE820), "b"(contID), "c"(24), "d"(0x534D4150));
      if (signature != 0x534D4150) {
        entries = -1; // error
        break;
      }
      if (bytes > 20 && (buffer->ACPI & 0x0001) == 0) {
      } else {
        buffer++;
        entries++;
      } 
    } while (false);

    args.GetReturnValue().Set(Number::New(args.GetIsolate(), buffer->LengthL + buffer->LengthH));
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

    // this is an escape hatch for printing when nothing else works
    global->Set(String::NewFromUtf8(isolate, "print"),
                FunctionTemplate::New(isolate, Print));

    global->Set(String::NewFromUtf8(isolate, "ticks"),
                FunctionTemplate::New(isolate, Ticks));

    global->Set(String::NewFromUtf8(isolate, "eval"),
                FunctionTemplate::New(isolate, Eval));

    global->Set(String::NewFromUtf8(isolate, "exec"),
                FunctionTemplate::New(isolate, RunInNewContext));

    global->Set(String::NewFromUtf8(isolate, "poll"),
                FunctionTemplate::New(isolate, Poll));

    global->Set(String::NewFromUtf8(isolate, "inb"),
                FunctionTemplate::New(isolate, InByte));

    global->Set(String::NewFromUtf8(isolate, "outb"),
                FunctionTemplate::New(isolate, OutByte));

    global->Set(String::NewFromUtf8(isolate, "readmem"),
                FunctionTemplate::New(isolate, ReadMem));

    global->Set(String::NewFromUtf8(isolate, "buff"),
                FunctionTemplate::New(isolate, Buffer));

    return global;
  };

  void Main(char* str) {
    Isolate* isolate = Isolate::New();
    {

        TryCatch try_catch;
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
            Handle<String> file = String::NewFromUtf8(isolate, "system.js");
            Handle<String> code = String::NewFromUtf8(isolate, str);

            // Handle<String> code = String::
            Handle<Script> script = Script::Compile(code, file);

            if(script.IsEmpty()) {
              ReportException(isolate, &try_catch);
            } else {
              // run script
              Handle<Value> result = script->Run();

              if(result.IsEmpty()) {
                assert(try_catch.HasCaught());
                ReportException(isolate, &try_catch);
              } else {
                assert(!try_catch.HasCaught());
                if (!result->IsUndefined()) {
                  // If all went well and the result wasn't undefined then print
                  // the returned value.
                  v8::String::Utf8Value str(result);
                  const char* cstr = ToCString(str);
                  printf("%s\n", cstr);
                }
              }
            }
        }

    }
    isolate->Dispose();

  };

}
