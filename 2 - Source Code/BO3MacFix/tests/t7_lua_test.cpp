#include "t7_lua.h"
#include "t7_runtime.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <vector>
#include <cstdint>
static std::vector<std::pair<uintptr_t,size_t>> readable_ranges;
extern "C" bool t7_memory_readable(const void *p,size_t n) {
    uintptr_t address=reinterpret_cast<uintptr_t>(p);
    for(auto range:readable_ranges) if(address>=range.first && address-range.first<=range.second && n<=range.second-(address-range.first)) return true;
    return false;
}
extern "C" bool t7_memory_writable(const void *p,size_t n) { return t7_memory_readable(p,n); }
extern "C" void *t7_resolve_function(const char *) { return nullptr; }
extern "C" void t7_log(const char *, ...) { }
extern "C" int DobbyHook(void *, void *, void **) { return -1; }
#include "../source/t7_lua.cpp"
static uint64_t vm_stack[8], moved_stack[8];
static bool relocate;
static void fake_push(int64_t *state,const char *,size_t) {
    auto *target=relocate?moved_stack:vm_stack;
    target[2]=9;target[3]=1234;
    state[9]=reinterpret_cast<int64_t>(target+4);
    state[10]=reinterpret_cast<int64_t>(target);
}
static void write_file(const char *path,const char *text) { FILE *f=fopen(path,"w");assert(f);assert(fputs(text,f)>=0);fclose(f); }
static void expect(const char *text,const char *mod,const char *stock,const char *expected) {
    char *result=t7_rewrite_trace(text,mod,stock);assert(result);assert(!strcmp(result,expected));free(result);
}
int main() {
    char mod[]="/tmp/bo3-lua-mod.XXXXXX",stock[]="/tmp/bo3-lua-stock.XXXXXX";
    int m=mkstemp(mod),s=mkstemp(stock);assert(m>=0&&s>=0);close(m);close(s);
    write_file(mod,"123,4,@ui\\test.lua,27,Test\r\n4294967296,4,bad,1,Bad\n");
    write_file(stock,"124,ui/stock.lua,16,StockFn\n125,ui/old.lua,OldFn\n126,ui/one.lua\n");
    expect("  123:4: in function '(*stripped)'\n",mod,stock,"  ui/test.lua:27: in function 'Test'\n");
    expect("124:7: in function '(*stripped)'",mod,stock,"ui/stock.lua:pc7: in function 'StockFn'");
    expect("125:7: in function '(*stripped)'",mod,stock,"ui/old.lua:pc7: in function 'OldFn'");
    expect("126:7: in function '(*stripped)'",mod,stock,"ui/one.lua:pc7: in function '(*stripped)'");
    expect("unknown\n4294967296:4: invalid",mod,stock,"unknown\n4294967296:4: invalid");
    write_file(mod,"123,4,one.lua,1,One\n123,4,two.lua,2,Two\n");
    expect("123:4: ambiguous",mod,nullptr,"123:4: ambiguous");
    write_file(stock,"123,one.lua,One\n123,two.lua,Two\n");
    expect("123:4: ambiguous",nullptr,stock,"123:4: ambiguous");
    write_file(stock,"123,one.lua,One\n123,one.lua,Two\n");
    expect("123:4: in function '(*stripped)'",nullptr,stock,"one.lua:pc4: in function '(*stripped)'");
    std::string too_large(1024*1024+1,'x');assert(!t7_rewrite_trace(too_large.c_str(),mod,stock));
    assert(!t7_rewrite_trace(nullptr,mod,stock));
    int64_t state[11]={0};
    readable_ranges={{reinterpret_cast<uintptr_t>(state),sizeof(state)},
        {reinterpret_cast<uintptr_t>(vm_stack),sizeof(vm_stack)},
        {reinterpret_cast<uintptr_t>(moved_stack),sizeof(moved_stack)}};
    push_string=fake_push;
    state[9]=reinterpret_cast<int64_t>(vm_stack+2);state[10]=reinterpret_cast<int64_t>(vm_stack);
    assert(replace_vm_top(state,"new trace"));
    assert(vm_stack[0]==9&&vm_stack[1]==1234&&state[9]==reinterpret_cast<int64_t>(vm_stack+2));
    relocate=true;
    assert(replace_vm_top(state,"relocated trace"));
    assert(moved_stack[0]==9&&moved_stack[1]==1234&&state[9]==reinterpret_cast<int64_t>(moved_stack+2));
    assert(!replace_vm_top(nullptr,"bad"));
    state[9]=state[10]-1;assert(!replace_vm_top(state,"invalid stack"));
    assert(!live_function_name(nullptr,nullptr));
    unlink(mod);unlink(stock);
    puts("Lua symbol recovery tests passed.");
}
