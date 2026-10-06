// Checks that Dobby relocates the prologues used by the built-in Mac map:
// a short jcc inside the patched bytes (LobbyMsgRW_Package* wrappers) and a
// RIP-relative lea (CL_GetConfigString).
#include "dobby.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>

// mov rax,rsi; mov ecx,[rdi+0x40]; test ecx,ecx; je 1f; cmp ecx,1; jne 2f; ...
__asm__(".text\n.globl _package_like\n_package_like:\n"
        "  mov %rsi, %rax\n  mov 0x40(%rdi), %ecx\n  test %ecx, %ecx\n  je 1f\n"
        "  cmp $1, %ecx\n  jne 2f\n  push %rbp\n  mov %rsp, %rbp\n  mov (%rdx), %eax\n"
        "  add $100, %eax\n  pop %rbp\n  ret\n1: xor %eax, %eax\n  ret\n2: mov $7, %eax\n  ret\n");
// push rbp; mov rbp,rsp; movsxd rax,edi; lea rcx,[rip+table]; mov eax,[rcx+rax*4]; pop rbp; ret
__asm__(".data\n_table: .long 11, 22, 33\n.text\n.globl _riprel_like\n_riprel_like:\n"
        "  push %rbp\n  mov %rsp, %rbp\n  movslq %edi, %rax\n  lea _table(%rip), %rcx\n"
        "  mov (%rcx,%rax,4), %eax\n  pop %rbp\n  ret\n");
// push rbp; mov rbp,rsp; push rbx; push rax; xor ebx,ebx; xor esi,esi; mov edx,10; call helper
// (Mods_SubscribeUGC: a rel32 call that straddles the patched bytes)
__asm__(".text\n_helper_add: lea 1(%rdi,%rdx), %rax\n  ret\n.globl _call_like\n_call_like:\n"
        "  push %rbp\n  mov %rsp, %rbp\n  push %rbx\n  push %rax\n  xor %ebx, %ebx\n  xor %esi, %esi\n"
        "  mov $10, %edx\n  call _helper_add\n  add $8, %rsp\n  pop %rbx\n  pop %rbp\n  ret\n");
int package_like(void *msg, const char *key, int *value);
long call_like(long x);
int riprel_like(int index);

static int (*orig_package)(void *, const char *, int *);
static int hook_package(void *m, const char *k, int *v) { return orig_package(m, k, v) + 1000; }
static long (*orig_call)(long);
static long hook_call(long x) { return orig_call(x) + 500; }
static int (*orig_riprel)(int);
static int hook_riprel(int i) { return orig_riprel(i) * 2; }

int main(void) {
    int fails = 0, value = 5; uint8_t msg[0x48] = {0};
    if (DobbyHook((void *)package_like, (void *)hook_package, (void **)&orig_package)) { puts("hook package failed"); return 1; }
    if (DobbyHook((void *)riprel_like, (void *)hook_riprel, (void **)&orig_riprel)) { puts("hook riprel failed"); return 1; }
    if (DobbyHook((void *)call_like, (void *)hook_call, (void **)&orig_call)) { puts("hook call failed"); return 1; }
    fails += call_like(5) != 516;                                                  // 5 + 10 + 1 + 500
    *(int *)(msg + 0x40) = 0; fails += package_like(msg, "k", &value) != 1000;      // je taken
    *(int *)(msg + 0x40) = 1; fails += package_like(msg, "k", &value) != 1105;      // fallthrough
    *(int *)(msg + 0x40) = 2; fails += package_like(msg, "k", &value) != 1007;      // jne taken
    fails += riprel_like(2) != 66;
    printf("dobby prologue test: %s\n", fails ? "FAILED" : "ok");
    return fails != 0;
}
