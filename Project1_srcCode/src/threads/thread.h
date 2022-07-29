#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* 실행중인 Thread Status이다. */
    THREAD_READY,       /* 현재 실행중은 아니나, Ready되어있는 Thread Status이다. */
    THREAD_BLOCKED,     /* Event가 발생하기 전까지 Block되어있는 Thread Status이다. */
    THREAD_DYING        /* 할당 해제될 Thread Status이다. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define NICE_DEFAULT 0
#define RECENT_CPU_DEFAULT 0
#define LOAD_AVG_DEFAULT 0

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread를 식별하는 고유 ID이다.(Thread ID)*/
    enum thread_status status;          /* Thread의 상태를 의미한다. (Running, Ready, Blocked, Dying)*/
    char name[16];                      /* Thread의 이름이다.*/
    uint8_t *stack;                     /* Thread의 Stack Pointer이다. */
    int priority;                       /* Thread의 Priority이다. Priority는 0 ~ 63사이의 값을 가지며, Default 값은 31이다. */ 
    struct list_elem allelem;           /* 모든 Thread가 담겨있는 List이다. Doubly Linked List를 기반으로 한다.*/

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* Wait Status의 Threads가 담겨있는 List이다. */
    /* Wait Status란 Ready와 Blocked Status를 의미한다. 이는 Dual Purpose로 사용될 수 있는데, 
       두 Status는 상호 배제적이므로 Ready만 담거나, Blocked만 담거나 할 수 있다. */

   int origin_priority;             //donation이전의 기존 priority
   struct list donation_list;       //thread에 priority를 donate한 thread들의 리스트
   struct list_elem donation_elem;  //위 list를 관리하기 위한 element
   struct lock *wait_lock;          //이 lock이 release될 때까지 thread는 기다린다.

   /*Variable for Advanced Scheduler*/
   int nice;
   int recent_cpu;

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
#endif

    /* Owned by thread.c. */
    unsigned magic;                     
    /* Thread의 Stack Overflow를 확인하는 값이다. Stack Pointer가 이 값을 가리키면 Overflow이다. */ 
    int64_t WakeUpTicks;
    /*Thread가 일어날 시간을 담는 Variable이다.*/
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void thread_sleep(int64_t ticks);
void thread_wakeup(int64_t ticks);
void thread_compare(void);
bool CompareWakeUpTick(struct list_elem *sleep_elem, struct list_elem *slept_elem, void *aux);
bool thread_comparepriority(struct list_elem *thread_1, struct list_elem *thread_2, void *aux);
bool thread_comparedonatepriority(struct list_elem *thread_1, struct list_elem *thread_2, void *aux);

void mlfqs_cal_priority(struct thread *thrd);
void mlfqs_cal_recent_cpu(struct thread *thrd);
void mlfqs_inc_recent_cpu();
void mlfqs_priority();
void mlfqs_recent_cpu();
void mlfqs_load_avg();

#endif /* threads/thread.h */
