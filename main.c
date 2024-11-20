// vim: set et ts=4 sw=4:
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

#ifdef USE_POSIX
/* this is doing some heavylifting for us. I would like to do it myself so that
 * I learn more.
 * */
#include <ucontext.h>
#else
/* This is the context handling code (simplistic replacement of ucontext.h)
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
    void *stack;
    unsigned long stack_size;
    unsigned long regs[NUM_REG];
} cntx_t;

void __should_never_called(void)
{
    printf("-- hehe, this is funny ;)\n");
}

static inline __attribute__((always_inline))
void getcontext(cntx_t *ctx)
{
    /* Save general purpose register
     * RSP,RBP,RIP,RFLAGS will be saved later
     * */
    __asm__ (
            "movq %%r8,  %0\n\t"
            "movq %%r9,  %1\n\t"
            "movq %%r10, %2\n\t"
            "movq %%r11, %3\n\t"
            "movq %%r12, %4\n\t"
            "movq %%r13, %5\n\t"
            "movq %%r14, %6\n\t"
            "movq %%r15, %7\n\t"
            "movq %%rdi, %8\n\t"
            "movq %%rsi, %9\n\t"
            "movq %%rbp, %10\n\t"
            "movq %%rsp, %11\n\t"
            "movq %%rbx, %12\n\t"
            "movq %%rdx, %13\n\t"
            "movq %%rcx, %14\n\t"
            "movq %%rax, %15\n\t"
            : /* Output operants */
            "=m" (ctx->regs[0]),
            "=m" (ctx->regs[1]),
            "=m" (ctx->regs[2]),
            "=m" (ctx->regs[3]),
            "=m" (ctx->regs[4]),
            "=m" (ctx->regs[5]),
            "=m" (ctx->regs[6]),
            "=m" (ctx->regs[7]),
            "=m" (ctx->regs[8]),
            "=m" (ctx->regs[9]),
            "=m" (ctx->regs[10]),
            "=m" (ctx->regs[11]),
            "=m" (ctx->regs[12]),
            "=m" (ctx->regs[13]),
            "=m" (ctx->regs[14]),
            "=m" (ctx->regs[15])
            : /* Input operands */
            : "memory" );
}

static inline __attribute__((always_inline))
void setcontext(cntx_t *ctx)
{
    __asm__ (
            /* "movq %0, %%r8\n\t" */
            "movq %1, %%r9\n\t"
            "movq %2, %%r10\n\t"
            "movq %3, %%r11\n\t"
            "movq %4, %%r12\n\t"
            "movq %5, %%r13\n\t"
            "movq %6, %%r14\n\t"
            "movq %7, %%r15\n\t"
            "movq %8, %%rdi\n\t"
            "movq %9, %%rsi\n\t"
            "movq %10, %%rbp\n\t"
            "movq %11, %%rsp\n\t"
            "movq %12, %%rbx\n\t"
            "movq %13, %%rdx\n\t"
            "movq %14, %%rcx\n\t"
            "movq %15, %%rax\n\t"
            : /* Output operand */
            : /* Input operand */
            "m" (ctx->regs[0]),
            "m" (ctx->regs[1]),
            "m" (ctx->regs[2]),
            "m" (ctx->regs[3]),
            "m" (ctx->regs[4]),
            "m" (ctx->regs[5]),
            "m" (ctx->regs[6]),
            "m" (ctx->regs[7]),
            "m" (ctx->regs[8]),
            "m" (ctx->regs[9]),
            "m" (ctx->regs[10]),
            "m" (ctx->regs[11]),
            "m" (ctx->regs[12]),
            "m" (ctx->regs[13]),
            "m" (ctx->regs[14]),
            "m" (ctx->regs[15])
            : /* I will update the return address which is on the memory */
            "memory");
}

/* Here we have the coroutine library definitions.
 * inspired by: https://codebrowser.dev/glibc/glibc/sysdeps/unix/sysv/linux/x86_64/makecontext.c.html
 * */
static void makecontext(cntx_t *ctx, void *func, size_t count_args, void *arg)
{
    assert (count_args == 1);
    char *sp;
    char *bp;
    sp = ctx->stack + ctx->stack_size;
    /* Save some space for link (?) */
    sp -= 8;
    /* align */
    sp = (char *)((unsigned long)sp & -16L);
    /* space for trampoline (function that we return to if the coroutine
     * return)
     * */
    sp -= 8;

    /* printf("makecontext: storing RIP=%p\n", func); */
    ctx->regs[REG_RIP] = (unsigned long)func;
    /* Why we are setting RBX? */
    ctx->regs[REG_RBX] = (unsigned long)(sp + 8);
    ctx->regs[REG_RSP] = (unsigned long)sp;
    /* when should I set RBP? what is the difference between RSP and RBP? */
    ctx->regs[REG_RBP] = (unsigned long)sp;

    /* What is a shadow stack ?? */

    *(unsigned long *)sp = (unsigned long)(&__should_never_called);
    *(unsigned long *)(sp + 8) = 0; /* There is no link */

    /* The first argument will be on RDI */
    ctx->regs[REG_RDI] = (unsigned long)arg;
}

/* Save the active context into `out' and replace it with `in'
 *
 * note: not inlining the function otherwise the `lbl1' symbol to be defined
 * multiple times (compile error).
 * */
static __attribute__((noinline))
void swapcontext(cntx_t *out, cntx_t *in)
{
    /* A local variable on the stack */
    unsigned long __tmp_reg;
    /* store context into out */
    getcontext(out);
    /* store lbl1 address as RIP. When we swap back the code will continue from
     * there.
     * Also the swap back procedure (asm below this), uses R8. we should store
     * it on the stack and restore it from the stack.
     *
     * note: using `$lbl1f' (absolute address of lbl1)
     * caused issue with generating PIE (place
     * independent executable). It tried to do a relative addressing.
     * https://stackoverflow.com/questions/71482016/relocation-r-x86-64-32s-against-symbol-stdoutglibc-2-2-5-can-not-be-used-whe
     * */
    __asm__("movq %%r8, %0\n\t"
            "lea lbl1(%%rip), %%r8\n\t"
            "movq %%r8, %1\n\t"
            : "=m" (__tmp_reg),
              "=m" (out->regs[REG_RIP])
            : /*no input*/
            : "memory"
            );
    printf("swapcontext: target RIP: %p\n", (void *)in->regs[REG_RIP]);
    /* move the target instruction address to R8 */
    __asm__("movq %0, %%r8\n\t"
            :
            : "m" (in->regs[REG_RIP])
            : );
    /* load register values (except R8) from the input context */
    setcontext(in);
    __asm__( "jmp *%%r8\n\t"
            : /* no output */
            :
            : "memory");
    /* Labeling the return address
     * When we switch back, we continue from here!
     * Restore the RAX value from the stack
     * */
    __asm__("lbl1: movq %0, %%r8"
            : /* not output */
            : "m" (__tmp_reg)
            : "memory"
            );
}
#endif

#ifndef bool
typedef char bool;
#define false 0
#define true 1
#endif

#ifdef USE_POSIX
static ucontext_t main_context;
#else
static cntx_t main_context;
#endif

struct coroutine;
typedef void (*corofunc)(struct coroutine *);
typedef struct coroutine {
    void* stack;
    size_t stack_size;
#ifdef USE_POSIX
    ucontext_t context;
#else
    cntx_t context;
#endif
    corofunc function;
    int yield_value;
    bool is_finished;
} coroutine_t;

coroutine_t* coroutine_create(corofunc* func, size_t stack_size)
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
    makecontext(&coro->context, (void (*)())func, 1, coro);

    return coro;
}

void coroutine_destroy(coroutine_t *coro)
{
    // TODO: make sure it is not the active context :)
    free(coro->stack);
    free(coro);
}

static inline int coroutine_yield(coroutine_t* coro, int value)
{
    coro->yield_value = value;
    swapcontext(&coro->context, &main_context);
    return coro->yield_value;
}

static inline int coroutine_resume(coroutine_t* coro)
{
    if (coro->is_finished) {
        return -1;  // Coroutine has finished
    }
    swapcontext(&main_context, &coro->context);
    return coro->yield_value;
}

static inline int coroutine_exit(coroutine_t *coro, int retcode)
{
    if (coro->is_finished) return coro->yield_value;
    coro->is_finished = true;
    return coroutine_yield(coro, retcode);
}

/* ---------------------------------------------------- */

/* Here is an example of application using the coroutine library
 * */
void example_coroutine(coroutine_t* coro) {
    printf("Coroutine started\n");
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
    // Get the main context that we are at righ now
    getcontext(&main_context);
    printf("About to lunch a coroutine...\n");
    coroutine_t* coro = coroutine_create(example_coroutine, 1024);
    if (coro == NULL) {
        printf("Failed to create the coroutine!\n");
        return 1;
    }
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
