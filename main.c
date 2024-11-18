// vim: set et ts=4 sw=4:
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
/* this is doing some heavylifting for us. I would like to do it myself so that
 * I learn more.
 * */
#include <ucontext.h>

#ifndef bool
typedef char bool;
#define false 0
#define true 1
#endif

static ucontext_t main_context;

struct coroutine;
typedef void (*corofunc)(struct coroutine *);
typedef struct coroutine {
    void* stack;
    size_t stack_size;
    ucontext_t context;
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
    coro->context.uc_stack.ss_sp = coro->stack;
    coro->context.uc_stack.ss_size = stack_size;
    coro->context.uc_link = NULL;
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

