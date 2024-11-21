/* vim: set et ts=4 sw=4: */
/* @description:
 *   In this project I am trying to implement a coroutine mechanism
 *   in C (For learning purposes).
 * @author: Farbod Shahinfar
 * @date: 2024
 * */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

/* #define USE_POSIX 1 */
#ifdef USE_POSIX
/* POSIX provides an API for managing a processing context. It is a nice
 * starting point because it is doing some heavylifting for us. But, eventually
 * I would like to do it myself so that I learn more.
 * */
#include <ucontext.h>
#else
/* This is the context handling code (simplistic replacement of ucontext.h)
 * The code is inspired by the glibc implementation
 * inspired by: https://codebrowser.dev/glibc/glibc/sysdeps/x86_64/sys/ucontext.h.html
 * */
#define NUM_REG 18
enum
{
  REG_R8 = 0,
  REG_R9,
  REG_R10,
  REG_R11,
  REG_R12,
  REG_R13,
  REG_R14,
  REG_R15,
  REG_RDI,
  REG_RSI,
  REG_RBP,
  REG_RSP,
  REG_RBX,
  REG_RDX,
  REG_RCX,
  REG_RAX,
  REG_RIP,
  REG_FLAGS,
};

typedef struct context {
    /* NOTE: in my implementation, it is important that registers defined at
     * the top.
     * */
    unsigned long regs[NUM_REG];
    unsigned long stack_size;
    void *stack;
} __attribute__((packed)) cntx_t;

void __should_never_called(void)
{
    printf("-- hehe, this is funny ;)\n");
    for (;;) {}
}

/* NOTE: always inline so that we record the right RSP and RBP */
static inline __attribute__((always_inline))
void getcontext(cntx_t *ctx)
{
    /* Get a snapshot of registers.
     *
     * NOTE: RDI is the first parameter of the function so it already holds the
     * pointer to context. The registers are stored at the top of the context.
     *
     * NOTE: Do not inline the function to make sure that RDI will be set to
     * the context pointer.
     * */
    __asm__ ("movq %%r8, 0(%0)\n\t"
            "movq %%r9,  8(%0)\n\t"
            "movq %%r10, 16(%0)\n\t"
            "movq %%r11, 24(%0)\n\t"
            "movq %%r12, 32(%0)\n\t"
            "movq %%r13, 40(%0)\n\t"
            "movq %%r14, 48(%0)\n\t"
            "movq %%r15, 56(%0)\n\t"
            "movq %%rdi, 64(%0)\n\t"
            "movq %%rsi, 72(%0)\n\t"
            "movq %%rbp, 80(%0)\n\t"
            "movq %%rsp, 88(%0)\n\t"
            "movq %%rbx, 96(%0)\n\t"
            "movq %%rdx, 104(%0)\n\t"
            "movq %%rcx, 112(%0)\n\t"
            "movq %%rax, 120(%0)\n\t"
            : /* Output operants */
            : /* Input operands */
            "r" (ctx)
            : "memory");
}

/* Prepare the stack (of a newly created coroutine) as if it is entering the
 * function
 * inspired by: https://codebrowser.dev/glibc/glibc/sysdeps/unix/sysv/linux/x86_64/makecontext.c.html
 * */
void makecontext(cntx_t *ctx, void *func, size_t count_args, void *arg)
{
    /* NOTE: The function only receives one argument of type pointer */
    assert (count_args == 1);
    char *sp;
    sp = ctx->stack + ctx->stack_size;
    /* align */
    sp = (char *)((unsigned long)sp & -16L);
    sp -= 128; /* red zone */
    /* space for return address */
    *(unsigned long *)sp = (unsigned long)(&__should_never_called);
    sp -= 8;
    /* Why the order is like this? First arguments and link, then aligning and
     * then return address ??
     * */

    ctx->regs[REG_RIP] = (unsigned long)func;
    ctx->regs[REG_RBP] = 0; /* the function prologue will update this */
    ctx->regs[REG_RSP] = (unsigned long)sp;

    /* printf("makecontext: RIP=%p\n", func); */
    /* printf("makecontext: RSP=%p\n", sp); */
    /* printf("makecontext: arg0=%p\n", arg); */

    /* What is a shadow stack ?? */

    /* The first argument will be on RDI */
    ctx->regs[REG_RDI] = (unsigned long)arg;
}

/* Save the active context into `out' and replace it with `in'
 *
 * NOTE: not inlining the function for multiple reasons:
 *  1. `lbl1' symbol to be defined multiple times (compile error).
 *  2. We rely on the calling convention (`out' in RDI and `in' in RSI)
 * */
__attribute__((noinline))
void swapcontext(cntx_t *out, cntx_t *in)
{
    /* store context into out */
    getcontext(out);

    /* store lbl1 address in RIP. When we swap back the code will continue from
     * there.
     *
     * NOTE: using `$lbl1f' (absolute address of lbl1)
     * caused issue with generating PIE (place
     * independent executable). I tried to do a relative addressing.
     * https://stackoverflow.com/questions/71482016/relocation-r-x86-64-32s-against-symbol-stdoutglibc-2-2-5-can-not-be-used-whe
     * */
    __asm__("lea lbl1(%%rip), %%r12\n\t"
            "movq %%r12, 128(%%rdi)\n\t"
            :
            : /* no input */
            : "r12", "memory");

    /* load register values from the input context */
    __asm__("movq 0(%%rsi), %%r8\n\t"
            "movq 8(%%rsi), %%r9\n\t"
            "movq 16(%%rsi), %%r10\n\t"
            "movq 24(%%rsi), %%r11\n\t"
            "movq 32(%%rsi), %%r12\n\t"
            "movq 40(%%rsi), %%r13\n\t"
            "movq 48(%%rsi), %%r14\n\t"
            "movq 56(%%rsi), %%r15\n\t"
            "movq 64(%%rsi), %%rdi\n\t"
            "movq 80(%%rsi), %%rbp\n\t"
            "movq 88(%%rsi), %%rsp\n\t"
            /* Let's ignore RBX */
            /* "movq 96(%%rsi), %%rbx\n\t" */
            "movq 104(%%rsi), %%rdx\n\t"
            "movq 112(%%rsi), %%rcx\n\t"
            "movq 120(%%rsi), %%rax\n\t"
            /* move the target RIP to the RBX (see comment below) */
            "movq 128(%%rsi), %%rbx\n\t"
            /* restore RSI value */
            "movq 72(%%rsi), %%rsi\n\t"
            : : :
            "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15", "rdi", "rsi",
        "rdx", "rcx", "rax", "rsp"
            );

    /* moved the target RIP to a tmp register (RBX is the second argument)
     * RIP is stored at offset=128 from top of the context structure.
     * NOTE: selecting the register to mess with seems to be important because
     * the program that we are jumping too may rely on it.
     * */

    /* Do an indirect jump to the new RIP (address stored in RBX) */
    __asm__( "jmpq *%%rbx\n\t" : : : );

    /* Labeling the return address.
     * When we switch back, we continue from here!
     * */
    __asm__("lbl1:\n\t" : : : );
    return;
}
#endif

/* Coroutine library built on top of context management helpers */
#ifndef bool
typedef char bool;
#define false 0
#define true 1
#endif

#ifdef USE_POSIX
typedef ucontext_t CONTEXT;
#else
typedef cntx_t CONTEXT;
#endif

static CONTEXT main_context;

struct coroutine;
typedef struct coroutine coroutine_t;
typedef void (*corofunc)(coroutine_t *);
struct coroutine {
    void* stack;
    size_t stack_size;
    CONTEXT context;
    corofunc function;
    int yield_value;
    bool is_finished;
};

static coroutine_t* coroutine_create(corofunc func, size_t stack_size)
{
    coroutine_t* coro = malloc(sizeof(coroutine_t));
    if (coro == NULL) return NULL;
    coro->stack = malloc(stack_size);
    if (coro->stack == NULL) {
        free(coro);
        return NULL;
    }
    coro->stack_size = stack_size;
    coro->function = func;
    coro->is_finished = false;

    getcontext(&coro->context);
#ifdef USE_POSIX
    coro->context.uc_stack.ss_sp = coro->stack;
    coro->context.uc_stack.ss_size = stack_size;
    coro->context.uc_link = NULL;
#else
    coro->context.stack = coro->stack;
    coro->context.stack_size = stack_size;
#endif
    makecontext(&coro->context, (void*)func, 1, coro);

    return coro;
}

static inline __attribute__((always_inline))
void coroutine_destroy(coroutine_t *coro)
{
    // TODO: make sure it is not the active context :)
    free(coro->stack);
    free(coro);
}

static inline __attribute__((always_inline))
int coroutine_yield(coroutine_t* coro, int value)
{
    coro->yield_value = value;
    swapcontext(&coro->context, &main_context);
    return coro->yield_value;
}

static inline __attribute__((always_inline))
int coroutine_resume(coroutine_t* coro)
{
    if (coro->is_finished) {
        return -1;  // Coroutine has finished
    }
    swapcontext(&main_context, &coro->context);
    return coro->yield_value;
}

static inline __attribute__((always_inline))
int coroutine_exit(coroutine_t *coro, int retcode)
{
    if (coro->is_finished) return coro->yield_value;
    coro->is_finished = true;
    return coroutine_yield(coro, retcode);
}
/* ---------------------------------------------------- */

/* Here is an example of application using the coroutine library
 * */
void example_coroutine(coroutine_t* coro) {
    printf("Coroutine started (arg0: %p)\n", coro);
    /* printf("-- think main is at %p\n", (void *)main_context.regs[REG_RIP]); */
    printf("Coroutine about to yield...\n");
    coroutine_yield(coro, 1);
    printf("Coroutine resumed\n");
    printf("Coroutine about to yield...\n");
    coroutine_yield(coro, 2);
    printf("Coroutine finished\n");
    /* Returing would close the program! */
    /* return 0; */
    coroutine_exit(coro, 0);
}

int main() {
    printf("Hello world!\n"
            "This is a test program demonstrating how a coroutine can be implemented\n");

    /* printf("main function at %p\n", &main); */
    /* printf("Targer coroutine is at %p\n", &example_coroutine); */

    printf("About to lunch a coroutine...\n");
    coroutine_t* coro = coroutine_create(example_coroutine, 1024);
    if (coro == NULL) {
        printf("Failed to create the coroutine!\n");
        return 1;
    }
    printf("Main: coroutine object is @ %p\n", coro);

    printf("Main: lunch a coroutine\n");
    int result = coroutine_resume(coro);
    printf("Main: Coroutine yielded %d\n", result);

    printf("Main: Resuming coroutine again\n");
    result = coroutine_resume(coro);
    printf("Main: Coroutine yielded %d\n", result);

    printf("Main: Resuming coroutine final time\n");
    result = coroutine_resume(coro);
    printf("Main: Coroutine finished (ret=%d)\n", result);
    /* printf("Main: Resuming finished coroutine\n"); */
    /* result = coroutine_resume(coro); */
    /* printf("Main: ret=%d\n", result); */

    coroutine_destroy(coro);
    return 0;
}
