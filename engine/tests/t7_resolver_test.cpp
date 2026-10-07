#include "t7_runtime.h"
extern "C" {
#include "utilities.h"
}
#include <cassert>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <sys/mman.h>
#include <unistd.h>
#include <mach-o/dyld.h>
extern "C" void t7_log(const char *, ...) {}
extern "C" int DobbyCodePatch(void *,uint8_t *,uint32_t){return 0;}
extern "C" __attribute__((used,noinline)) int MacFixResolverFixture(int n){return n+1;}
__attribute__((used,noinline)) int AmbiguousFixture(int n){return n+2;}
__attribute__((used,noinline)) int AmbiguousFixture(double n){return (int)n+3;}
int main(int argc,char **argv){
    bool bad_hash=argc>1&&!strcmp(argv[1],"bad-hash");
    char path[]="/tmp/bo3-symbols.XXXXXX";int fd=mkstemp(path);assert(fd>=0);
    FILE *map=fdopen(fd,"w");assert(map);
    uint8_t digest[20];char hash[41],prologue[17];
    assert(get_file_sha1(_dyld_get_image_name(0),digest));
    assert(bytes_to_hex(digest,20,hash,sizeof(hash)));
    assert(bytes_to_hex(reinterpret_cast<const uint8_t *>(MacFixResolverFixture),8,prologue,sizeof(prologue)));
    if(bad_hash)hash[0]=hash[0]=='0'?'1':'0';
    uintptr_t rva=reinterpret_cast<uintptr_t>(MacFixResolverFixture)-get_base_address();
    fprintf(map,"sha1 %s\nMapOnlyAlias %llx %s\nBadPrologue %llx 0000000000000000\nOverflowRva ffffffffffffffff %s\n",hash,(unsigned long long)rva,prologue,(unsigned long long)rva,prologue);
    fclose(map);assert(!setenv("BO3MACFIX_SYMBOL_MAP",path,1));
    assert(t7_resolve_function("MacFixResolverFixture")==reinterpret_cast<void *>(MacFixResolverFixture));
    assert(!t7_resolve_function("AmbiguousFixture"));
    assert(!t7_resolve_function("UnknownFixture"));
    assert(!t7_resolve_function("BadPrologue"));
    assert(!t7_resolve_function("OverflowRva"));
    assert(t7_resolve_function("MapOnlyAlias")== (bad_hash?nullptr:reinterpret_cast<void *>(MacFixResolverFixture)));
    int readable=123;assert(t7_memory_readable(&readable,sizeof(readable)));
    assert(!t7_memory_readable(nullptr,1));
    assert(!t7_memory_readable(&readable,SIZE_MAX));
    void *page=mmap(nullptr,4096,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0);assert(page!=MAP_FAILED);
    assert(!t7_memory_readable(page,1));munmap(page,4096);
    unlink(path);
    puts(bad_hash?"Resolver rejected mismatched executable map.":"Mach-O symbol resolver and fingerprint tests passed.");
}
