// Copyright 2014 Runtime.JS project authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <kernel/kernel-main.h>

#include <libc.h>
#include <stdio.h>

#include <kernel/keystorage.h>
#include <kernel/initrd.h>
#include <kernel/engines.h>
#include <kernel/trace.h>
#include <kernel/kernel.h>
#include <kernel/multiboot.h>
#include <kernel/version.h>
#include <kernel/boot-services.h>
#include <kernel/logger.h>
#include <kernel/platform.h>
#include <kernel/irqs.h>

#include <test-framework.h>

#define DEFINE_GLOBAL_OBJECT(name, type)                       \
    static uint8_t placement_##name[sizeof(type)] alignas(16); \
    type* intr_##name = nullptr

DEFINE_GLOBAL_OBJECT(GLOBAL_platform, rt::Platform);
DEFINE_GLOBAL_OBJECT(GLOBAL_boot_services, rt::BootServices);
DEFINE_GLOBAL_OBJECT(GLOBAL_multiboot, rt::Multiboot);
DEFINE_GLOBAL_OBJECT(GLOBAL_mem_manager, rt::MemManager);
DEFINE_GLOBAL_OBJECT(GLOBAL_irqs, rt::Irqs);
DEFINE_GLOBAL_OBJECT(GLOBAL_keystorage, rt::KeyStorage);
DEFINE_GLOBAL_OBJECT(GLOBAL_initrd, rt::Initrd);
DEFINE_GLOBAL_OBJECT(GLOBAL_engines, rt::Engines);
DEFINE_GLOBAL_OBJECT(GLOBAL_trace, rt::Trace);

#undef DEFINE_GLOBAL_OBJECT

#define CONSTRUCT_GLOBAL_OBJECT(name, type, param)              \
    memset(&placement_##name, 0, sizeof(placement_##name));     \
    intr_##name = new (&placement_##name) type(param);          \
    RT_ASSERT(reinterpret_cast<void*>(&placement_##name)        \
            == reinterpret_cast<void*>(name()));                \
    RT_ASSERT(intr_##name)

typedef void (*function_pointer) (void);
extern function_pointer start_ctors[];
extern function_pointer end_ctors[];

int mksnapshot_main(int argc, char** argv);

namespace rt {

void KernelMain::Initialize(void* mbt) {
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_boot_services, BootServices, );      // NOLINT
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_multiboot, Multiboot, mbt);			// NOLINT
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_mem_manager, MemManager, );          // NOLINT

    Cpu::DisableInterrupts();
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_irqs, Irqs, );  					    // NOLINT

    // Initialize memory manager for this CPU
    // After this line we can use malloc / free to allocate memory
    GLOBAL_mem_manager()->InitSubsystems();

    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_keystorage, KeyStorage, );           // NOLINT
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_initrd, Initrd, );                   // NOLINT
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_trace, Trace, );                     // NOLINT

    // This will run V8 static constructors
    uint64_t ctor_count = (end_ctors - start_ctors);
    for (uint64_t x = 0; x < ctor_count; x++) {
        function_pointer constructor = start_ctors[x];
        constructor();
    }
}

MultibootParseResult KernelMain::ParseMultiboot(void* mbt) {
    MultibootStruct* s = reinterpret_cast<MultibootStruct*>(mbt);
    RT_ASSERT(s);
    uint32_t mod_count = s->module_count;
    uint32_t mod_addr = s->module_addr;

    if (0 == mod_count || 0 == mod_addr) {
        printf("Initrd boot module required. Check your bootloader configuration.\n");
        abort();
    }

    const char* cmd = nullptr;
    uint32_t cmdaddr = s->cmdline;
    if (0 != cmdaddr) {
        cmd = reinterpret_cast<const char*>(cmdaddr);
    } else {
        cmd = "";
    }

    RT_ASSERT(cmd);

    MultibootModuleEntry* m = reinterpret_cast<MultibootModuleEntry*>(mod_addr);
    RT_ASSERT(m);
    uint32_t rd_start = m->start;
    uint32_t rd_end = m->end;

    // This might be useful if initrd is unable to load files
    // rd_start = mod_addr;

    size_t len = rd_end - rd_start;

    if (0 == len || len  > 128 * common::Constants::MiB) {
        printf("Invalid initrd boot module.\n");
        abort();
    }

    GLOBAL_initrd()->Init(reinterpret_cast<void*>(rd_start), len);
    return MultibootParseResult(cmd);
}

void KernelMain::InitSystemBSP(void* mbt) {
    // some musl libc init
    libc.threads_minus_1 = 0;

    Initialize(mbt);
    MultibootParseResult parsed = ParseMultiboot(mbt);
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_platform, Platform, );		        // NOLINT

    // uint32_t cpus_found = GLOBAL_platform()->cpu_count();
    GLOBAL_platform()->InitCurrentCPU();

    // const char* cmdline = parsed.cmdline();

    // GLOBAL_boot_services()->logger()->EnableConsole();
    CONSTRUCT_GLOBAL_OBJECT(GLOBAL_engines, Engines, 1 /*cpus_found*/ );
    Cpu::EnableInterrupts();
    // GLOBAL_engines()->Startup();

    // Uncomment to enable SMP
    // GLOBAL_platform()->StartCPUs();
}

void KernelMain::InitSystemAP() {
    GLOBAL_mem_manager()->InitSubsystems();
    GLOBAL_platform()->InitCurrentCPU();
}


// spaces and ending with a newline.
void Print(const v8::FunctionCallbackInfo<v8::Value>& args) {
  v8::HandleScope scope(args.GetIsolate());
  v8::String::Utf8Value val(args[0]->ToString());

  printf("%s", *val);
}

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
}

static void GlobalPropertyGetterCallback(v8::Local<v8::String> property, const v8::PropertyCallbackInfo<v8::Value>& args) {
  using namespace v8;

  v8::Isolate* isolate = args.GetIsolate();
  v8::HandleScope scope(isolate);

  Handle<Object> obj = args.Data()->ToObject();

  args.GetReturnValue().Set(obj->Get(property));
}

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

}

// interrupt events
class Event {
public:
  Event(uint8_t val, uint64_t num): value(val), number(num) {}

  uint8_t value;
  uint64_t number;
};

// this is our event queue
static std::vector<uint64_t> queue;

static void InB(const v8::FunctionCallbackInfo<v8::Value>& args) {
  uint8_t port = args[0]->ToNumber()->Value();
  uint8_t value;

  // get the input
  asm volatile("inb %w1, %b0": "=a"(value): "d"(port));

  args.GetReturnValue().Set(v8::Number::New(args.GetIsolate(), value));
}

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
}

// JACOB: this runs in the interrupt handler
extern "C" void irq_handler_any(uint64_t number) {
    queue.push_back(number);

    // https://github.com/runtimejs/runtime/blob/master/initrd/system/driver/ps2kbd.js#L155
    GLOBAL_platform()->ackIRQ();
}

KernelMain::KernelMain(void* mbt) {
    uint32_t cpuid = Cpu::id();

    InitSystemBSP(mbt);

    rt::InitrdFile startup_file = GLOBAL_initrd()->Get("/hello.js");

    size_t size = startup_file.Size();
    const uint8_t* data = startup_file.Data();

    uint8_t place[size + 1];
    place[size] = '\0';
    memcpy(place, data, size);

    v8::Isolate* isolate = v8::Isolate::New();
    {
        v8::Locker locker(isolate);

        v8::Isolate::Scope isolateScope(isolate);
        v8::HandleScope handleScope(isolate);

        // create a global object templates to pass into the context
        v8::Handle<v8::ObjectTemplate> global = v8::ObjectTemplate::New(isolate);

        global->Set(v8::String::NewFromUtf8(isolate, "print"),
                    v8::FunctionTemplate::New(isolate, Print));

        global->Set(v8::String::NewFromUtf8(isolate, "load"),
                    v8::FunctionTemplate::New(isolate, InitrdLoad));

        global->Set(v8::String::NewFromUtf8(isolate, "exec"),
                    v8::FunctionTemplate::New(isolate, MakeContext));

        global->Set(v8::String::NewFromUtf8(isolate, "poll"),
                    v8::FunctionTemplate::New(isolate, Poll));

        global->Set(v8::String::NewFromUtf8(isolate, "inb"),
                    v8::FunctionTemplate::New(isolate, InB));

        v8::Handle<v8::Context> context = v8::Context::New(isolate, NULL, global);
        {
            // setup the context
            v8::Context::Scope contextScope(context);

            // compile the script from the initrd file
            v8::Handle<v8::String> file = v8::String::NewFromUtf8(isolate, "hello.js");
            v8::Handle<v8::String> code = v8::String::NewFromOneByte(isolate, place);
            v8::Handle<v8::Script> script = v8::Script::Compile(code, file);

            // run script
            v8::Handle<v8::Value> ret = script->Run();
            v8::String::Utf8Value val(ret->ToString());

            printf("Exit Main: %s\n", *val);
        }

    }

    isolate->Dispose();

}

} // namespace rt
