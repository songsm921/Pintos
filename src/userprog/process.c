#include "userprog/process.h"
#include <debug.h>
#include <inttypes.h>
#include <round.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "userprog/gdt.h"
#include "userprog/pagedir.h"
#include "userprog/tss.h"
#include "userprog/syscall.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/flags.h"
#include "threads/init.h"
#include "threads/interrupt.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "vm/page.h"
#include "vm/swap.h"
#include "vm/frame.h"

static thread_func start_process NO_RETURN;
static bool load (const char *cmdline, void (**eip) (void), void **esp);
extern struct lock lock_file;

void construct_esp(char *file_name, void **esp) {

  int argc;
  char ** argv;
  char *name;
  char *token;
  char *remain;
  int i;
  int len;
  
  name = palloc_get_page(PAL_ZERO);

  argc = 0;
  strlcpy(name, file_name, strlen(file_name) + 1);
  for(token = strtok_r(name, " ", &remain); token != NULL; token = strtok_r(NULL, " ", &remain)){
    if(*token != " ")
      argc++;
  }

  i = 0;
  argv = (char **)palloc_get_page(PAL_ZERO);
  strlcpy(name, file_name, strlen(file_name) + 1);
  for (token = strtok_r(name, " ", &remain); i < argc; token = strtok_r(NULL, " ", &remain)) {
    len = strlen(token);
    argv[i++] = token;
  }

  for (i = argc - 1; i >= 0; i--) {
    len = strlen(argv[i]);
    *esp -= len + 1;
    strlcpy(*esp, argv[i], len + 1);
    argv[i] = *esp;
  }

  *esp -= ((uint32_t)*esp) % 4;
  
  *esp -= 4;
  **(uint32_t **)esp = 0;
  
  for (i = argc - 1; i >= 0; i--) {
    *esp -= 4;
    **(uint32_t **)esp = argv[i];
  }
  
  *esp -= 4;
  **(uint32_t **)esp = *esp + 4;

  *esp -= 4;
  **(uint32_t **)esp = argc;
  
  *esp -= 4;
  **(uint32_t **)esp = 0;

  palloc_free_page(name);
  palloc_free_page(argv);
}

/* Starts a new thread running a user program loaded from
   FILENAME.  The new thread may be scheduled (and may even exit)
   before process_execute() returns.  Returns the new process's
   thread id, or TID_ERROR if the thread cannot be created. */
tid_t
process_execute (const char *file_name) 
{ 
  tid_t tid;
  char *fn_copy_1;
  char *fn_copy_2;
  char *name;
  char *remain;
  struct thread* child;

  /* Make a copy of FILE_NAME.
     Otherwise there's a race between the caller and load(). */
  fn_copy_1 = palloc_get_page(PAL_ZERO);
  fn_copy_2 = palloc_get_page(PAL_ZERO);
  if (fn_copy_1 == NULL)
    return TID_ERROR;
  strlcpy(fn_copy_1,file_name,PGSIZE);
  strlcpy(fn_copy_2,file_name,PGSIZE);

  name = strtok_r(fn_copy_2," ",&remain);
  /* Create a new thread to execute FILE_NAME. */
  tid = thread_create (name, PRI_DEFAULT, start_process, fn_copy_1);

  palloc_free_page(fn_copy_2);
  if (tid == TID_ERROR)
    palloc_free_page (fn_copy_1); 

  struct list_elem *e;
  // struct thread *child;
  for(e = list_begin(&thread_current()->child_list);e!= list_end(&thread_current()->child_list);e=list_next(e))
  {
    child = list_entry(e, struct thread, child_elem);
    if(child->exit_status == -1)
      return process_wait(tid);
  }

  return tid;
}

/* A thread function that loads a user process and starts it
   running. */
static void
start_process (void *file_name_)
{
  char *file_name = file_name_;
  struct intr_frame if_;
  bool success;

  vm_init(&thread_current()->vm);

  char* fn_copy = palloc_get_page(PAL_ZERO);
  char* cmd_name; // 4KB
  char *remain;
  strlcpy(fn_copy,file_name,PGSIZE);
  cmd_name = strtok_r(fn_copy," ",&remain);

  /* Initialize interrupt frame and load executable. */
  memset (&if_, 0, sizeof if_);
  if_.gs = if_.fs = if_.es = if_.ds = if_.ss = SEL_UDSEG;
  if_.cs = SEL_UCSEG;
  if_.eflags = FLAG_IF | FLAG_MBS;
  success = load (cmd_name, &if_.eip, &if_.esp);

  thread_current()->isLoad = success;
  if(success){
    construct_esp(file_name, &if_.esp);
  }
  sema_up(&thread_current()->sema_load);

  palloc_free_page(fn_copy);
  palloc_free_page(file_name);

  if (!success) {
    thread_exit ();
  }
    
  /* Start the user process by simulating a return from an
     interrupt, implemented by intr_exit (in
     threads/intr-stubs.S).  Because intr_exit takes all of its
     arguments on the stack in the form of a `struct intr_frame',
     we just point the stack pointer (%esp) to our stack frame
     and jump to it. */
  asm volatile ("movl %0, %%esp; jmp intr_exit" : : "g" (&if_) : "memory");
  NOT_REACHED ();
}

/* Waits for thread TID to die and returns its exit status.  If
   it was terminated by the kernel (i.e. killed due to an
   exception), returns -1.  If TID is invalid or if it was not a
   child of the calling process, or if process_wait() has already
   been successfully called for the given TID, returns -1
   immediately, without waiting.

   This function will be implemented in problem 2-2.  For now, it
   does nothing. */

int
process_wait (tid_t child_tid) 
{
  struct thread *parent = thread_current();
  struct thread *child;
  
  int status;
  struct list_elem *e;
  if (!(child = get_child_process(child_tid))) return -1;
  sema_down(&child->sema_exit);
  status = child->exit_status;
  remove_child_process(child);

  return status;
}


/* Free the current process's resources. */
void
process_exit (void)
{
  struct thread *cur = thread_current ();
  uint32_t *pd;

  int i;
  for(i = 2; i < cur->fd_nxt; i++) process_close_file(i);/* file descriptor 테이블의 최대값을 이용해 file descriptor의 최소값인 2가 될 때까지 파일을 닫음 */
	
  palloc_free_page(cur->fd_table); /* file descriptor 테이블 메모리 해제 */
  
  for (i = 1; i < cur->mmap_nxt; i++) munmap(i);
  file_close(cur->file_run);

  vm_destroy(&cur->vm);

  /* Destroy the current process's page directory and switch back
     to the kernel-only page directory. */
  pd = cur->pagedir;
  if (pd != NULL) 
    {
      /* Correct ordering here is crucial.  We must set
         cur->pagedir to NULL before switching page directories,
         so that a timer interrupt can't switch back to the
         process page directory.  We must activate the base page
         directory before destroying the process's page
         directory, or our active page directory will be one
         that's been freed (and cleared). */
      cur->pagedir = NULL;
      pagedir_activate (NULL);
      pagedir_destroy (pd);
    }
}

/* Sets up the CPU for running user code in the current
   thread.
   This function is called on every context switch. */
void
process_activate (void)
{
  struct thread *t = thread_current ();

  /* Activate thread's page tables. */
  pagedir_activate (t->pagedir);

  /* Set thread's kernel stack for use in processing
     interrupts. */
  tss_update ();
}

/* We load ELF binaries.  The following definitions are taken
   from the ELF specification, [ELF1], more-or-less verbatim.  */

/* ELF types.  See [ELF1] 1-2. */
typedef uint32_t Elf32_Word, Elf32_Addr, Elf32_Off;
typedef uint16_t Elf32_Half;

/* For use with ELF types in printf(). */
#define PE32Wx PRIx32   /* Print Elf32_Word in hexadecimal. */
#define PE32Ax PRIx32   /* Print Elf32_Addr in hexadecimal. */
#define PE32Ox PRIx32   /* Print Elf32_Off in hexadecimal. */
#define PE32Hx PRIx16   /* Print Elf32_Half in hexadecimal. */

/* Executable header.  See [ELF1] 1-4 to 1-8.
   This appears at the very beginning of an ELF binary. */
struct Elf32_Ehdr
  {
    unsigned char e_ident[16];
    Elf32_Half    e_type;
    Elf32_Half    e_machine;
    Elf32_Word    e_version;
    Elf32_Addr    e_entry;
    Elf32_Off     e_phoff;
    Elf32_Off     e_shoff;
    Elf32_Word    e_flags;
    Elf32_Half    e_ehsize;
    Elf32_Half    e_phentsize;
    Elf32_Half    e_phnum;
    Elf32_Half    e_shentsize;
    Elf32_Half    e_shnum;
    Elf32_Half    e_shstrndx;
  };

/* Program header.  See [ELF1] 2-2 to 2-4.
   There are e_phnum of these, starting at file offset e_phoff
   (see [ELF1] 1-6). */
struct Elf32_Phdr
  {
    Elf32_Word p_type;
    Elf32_Off  p_offset;
    Elf32_Addr p_vaddr;
    Elf32_Addr p_paddr;
    Elf32_Word p_filesz;
    Elf32_Word p_memsz;
    Elf32_Word p_flags;
    Elf32_Word p_align;
  };

/* Values for p_type.  See [ELF1] 2-3. */
#define PT_NULL    0            /* Ignore. */
#define PT_LOAD    1            /* Loadable segment. */
#define PT_DYNAMIC 2            /* Dynamic linking info. */
#define PT_INTERP  3            /* Name of dynamic loader. */
#define PT_NOTE    4            /* Auxiliary info. */
#define PT_SHLIB   5            /* Reserved. */
#define PT_PHDR    6            /* Program header table. */
#define PT_STACK   0x6474e551   /* Stack segment. */

/* Flags for p_flags.  See [ELF3] 2-3 and 2-4. */
#define PF_X 1          /* Executable. */
#define PF_W 2          /* Writable. */
#define PF_R 4          /* Readable. */

static bool setup_stack (void **esp);
static bool validate_segment (const struct Elf32_Phdr *, struct file *);
static bool load_segment (struct file *file, off_t ofs, uint8_t *upage,
                          uint32_t read_bytes, uint32_t zero_bytes,
                          bool writable);

/* Loads an ELF executable from FILE_NAME into the current thread.
   Stores the executable's entry point into *EIP
   and its initial stack pointer into *ESP.
   Returns true if successful, false otherwise. */
bool
load (const char *file_name, void (**eip) (void), void **esp) 
{
  struct thread *t = thread_current ();
  struct Elf32_Ehdr ehdr;
  struct file *file = NULL;
  off_t file_ofs;
  bool success = false;
  int i;

  /* Allocate and activate page directory. */
  t->pagedir = pagedir_create ();
  if (t->pagedir == NULL) 
    goto done;
  process_activate ();

  lock_acquire(&lock_file); /* 락 획득 */
  /* Open executable file. */
  file = filesys_open (file_name);
  if (file == NULL) 
    {
      lock_release(&lock_file);/* 락 해제 */
      printf ("load: %s: open failed\n", file_name);
      goto done; 
    }
  
  t->file_run = file;    /* thread 구조체의 run_file을 현재 실행할 파일로 초기화 */
  file_deny_write(file);  /* file_deny_write()를 이용하여 파일에 대한 write를 거부 */

  lock_release(&lock_file);/* 락 해제 */


  /* Read and verify executable header. */
  if (file_read (file, &ehdr, sizeof ehdr) != sizeof ehdr
      || memcmp (ehdr.e_ident, "\177ELF\1\1\1", 7)
      || ehdr.e_type != 2
      || ehdr.e_machine != 3
      || ehdr.e_version != 1
      || ehdr.e_phentsize != sizeof (struct Elf32_Phdr)
      || ehdr.e_phnum > 1024) 
    {
      printf ("load: %s: error loading executable\n", file_name);
      goto done; 
    }

  /* Read program headers. */
  file_ofs = ehdr.e_phoff;
  for (i = 0; i < ehdr.e_phnum; i++) 
    {
      struct Elf32_Phdr phdr;

      if (file_ofs < 0 || file_ofs > file_length (file))
        goto done;
      file_seek (file, file_ofs);

      if (file_read (file, &phdr, sizeof phdr) != sizeof phdr)
        goto done;
      file_ofs += sizeof phdr;
      switch (phdr.p_type) 
        {
        case PT_NULL:
        case PT_NOTE:
        case PT_PHDR:
        case PT_STACK:
        default:
          /* Ignore this segment. */
          break;
        case PT_DYNAMIC:
        case PT_INTERP:
        case PT_SHLIB:
          goto done;
        case PT_LOAD:
          if (validate_segment (&phdr, file)) 
            {
              bool writable = (phdr.p_flags & PF_W) != 0;
              uint32_t file_page = phdr.p_offset & ~PGMASK;
              uint32_t mem_page = phdr.p_vaddr & ~PGMASK;
              uint32_t page_offset = phdr.p_vaddr & PGMASK;
              uint32_t read_bytes, zero_bytes;
              if (phdr.p_filesz > 0)
                {
                  /* Normal segment.
                     Read initial part from disk and zero the rest. */
                  read_bytes = page_offset + phdr.p_filesz;
                  zero_bytes = (ROUND_UP (page_offset + phdr.p_memsz, PGSIZE)
                                - read_bytes);
                }
              else 
                {
                  /* Entirely zero.
                     Don't read anything from disk. */
                  read_bytes = 0;
                  zero_bytes = ROUND_UP (page_offset + phdr.p_memsz, PGSIZE);
                }
              if (!load_segment (file, file_page, (void *) mem_page,
                                 read_bytes, zero_bytes, writable))
                goto done;
            }
          else
            goto done;
          break;
        }
    }

  /* Set up stack. */
  if (!setup_stack (esp))
    goto done;

  /* Start address. */
  *eip = (void (*) (void)) ehdr.e_entry;

  success = true;

 done:
  /* We arrive here whether the load is successful or not. */
  // file_close (file); //Changed
  return success;
}

/* load() helpers. */

static bool install_page (void *upage, void *kpage, bool writable);

/* Checks whether PHDR describes a valid, loadable segment in
   FILE and returns true if so, false otherwise. */
static bool
validate_segment (const struct Elf32_Phdr *phdr, struct file *file) 
{
  /* p_offset and p_vaddr must have the same page offset. */
  if ((phdr->p_offset & PGMASK) != (phdr->p_vaddr & PGMASK)) 
    return false; 

  /* p_offset must point within FILE. */
  if (phdr->p_offset > (Elf32_Off) file_length (file)) 
    return false;

  /* p_memsz must be at least as big as p_filesz. */
  if (phdr->p_memsz < phdr->p_filesz) 
    return false; 

  /* The segment must not be empty. */
  if (phdr->p_memsz == 0)
    return false;
  
  /* The virtual memory region must both start and end within the
     user address space range. */
  if (!is_user_vaddr ((void *) phdr->p_vaddr))
    return false;
  if (!is_user_vaddr ((void *) (phdr->p_vaddr + phdr->p_memsz)))
    return false;

  /* The region cannot "wrap around" across the kernel virtual
     address space. */
  if (phdr->p_vaddr + phdr->p_memsz < phdr->p_vaddr)
    return false;

  /* Disallow mapping page 0.
     Not only is it a bad idea to map page 0, but if we allowed
     it then user code that passed a null pointer to system calls
     could quite likely panic the kernel by way of null pointer
     assertions in memcpy(), etc. */
  if (phdr->p_vaddr < PGSIZE)
    return false;

  /* It's okay. */
  return true;
}

/* Loads a segment starting at offset OFS in FILE at address
   UPAGE.  In total, READ_BYTES + ZERO_BYTES bytes of virtual
   memory are initialized, as follows:

        - READ_BYTES bytes at UPAGE must be read from FILE
          starting at offset OFS.

        - ZERO_BYTES bytes at UPAGE + READ_BYTES must be zeroed.

   The pages initialized by this function must be writable by the
   user process if WRITABLE is true, read-only otherwise.

   Return true if successful, false if a memory allocation error
   or disk read error occurs. */
static bool
load_segment (struct file *file, off_t ofs, uint8_t *upage,
              uint32_t read_bytes, uint32_t zero_bytes, bool writable) 
{
  ASSERT ((read_bytes + zero_bytes) % PGSIZE == 0);
  ASSERT (pg_ofs (upage) == 0);
  ASSERT (ofs % PGSIZE == 0);

  file_seek (file, ofs);
  while (read_bytes > 0 || zero_bytes > 0) 
    {
      /* Calculate how to fill this page.
         We will read PAGE_READ_BYTES bytes from FILE
         and zero the final PAGE_ZERO_BYTES bytes. */
      size_t page_read_bytes = read_bytes < PGSIZE ? read_bytes : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      // /* Get a page of memory. */
      // uint8_t *kpage = palloc_get_page (PAL_USER);
      // if (kpage == NULL)
      //   return false;

      // /* Load this page. */
      // if (file_read (file, kpage, page_read_bytes) != (int) page_read_bytes)
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }
      // memset (kpage + page_read_bytes, 0, page_zero_bytes);

      // /* Add the page to the process's address space. */
      // if (!install_page (upage, kpage, writable)) 
      //   {
      //     palloc_free_page (kpage);
      //     return false; 
      //   }

      /* vm_entry 생성 */
      struct vm_entry *vme = make_vme(VM_BIN, upage, writable, false, file, ofs, page_read_bytes, page_zero_bytes);
      if(!vme) return false;

      /* insert_vme() 함수를 사용해서 생성한 vm_entry를 hash table에 추가 */
      insert_vme (&thread_current ()->vm, vme);

      /* Advance. */
      read_bytes -= page_read_bytes;
      zero_bytes -= page_zero_bytes;
      ofs += page_read_bytes;
      upage += PGSIZE;
    }
  return true;
}

/* Create a minimal stack by mapping a zeroed page at the top of
   user virtual memory. */
static bool
setup_stack(void **esp)
{
  struct page *kpage;
  bool success = false;

  kpage = alloc_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL)
  {
    success = install_page(((uint8_t *)PHYS_BASE) - PGSIZE, kpage->kaddr, true);
    if (success)
      *esp = PHYS_BASE;
    else
    {
      free_page(kpage->kaddr);
      return success;
    }
  }
  else
    return success;

  /* vm_entry 생성 */
  kpage->vme = make_vme(VM_ANON, ((uint8_t *)PHYS_BASE) - PGSIZE, true, true, NULL, NULL, 0, 0);
  if (!kpage->vme)  return false;

  /* insert_vme() 함수로 hash table에 추가 */
  insert_vme(&thread_current()->vm, kpage->vme);
  return success;
}

bool verify_stack(uint32_t addr, void *esp)
{
  uint32_t stack_start = 0xC0000000;
  uint32_t stack_limit = 0x8000000;

  if (!is_user_vaddr(addr))
    return false; //address < stack_start
  if (addr < stack_start - stack_limit)
    return false;
  if (addr < esp - 32)
    return false;

  return true;
}

bool expand_stack(void *addr)
{
  struct page *kpage;
  void *upage = pg_round_down(addr);
  bool success = false;
  
  kpage = alloc_page(PAL_USER | PAL_ZERO);
  if (kpage != NULL)
  {
    success = install_page(upage, kpage->kaddr, true);
    if (!success)
    {
      free_page(kpage->kaddr);
      return success;
    }
  }
  else
    return success;

  /* vm_entry 생성 */
  kpage->vme = make_vme(VM_ANON, upage, true, true, NULL, NULL, 0, 0);
  if (!kpage->vme)  return false;

  /* insert_vme() 함수로 hash table에 추가 */
  insert_vme(&thread_current()->vm, kpage->vme);
  return success; 
}


/* Adds a mapping from user virtual address UPAGE to kernel
   virtual address KPAGE to the page table.
   If WRITABLE is true, the user process may modify the page;
   otherwise, it is read-only.
   UPAGE must not already be mapped.
   KPAGE should probably be a page obtained from the user pool
   with palloc_get_page().
   Returns true on success, false if UPAGE is already mapped or
   if memory allocation fails. */
static bool
install_page (void *upage, void *kpage, bool writable)
{
  struct thread *t = thread_current ();

  /* Verify that there's not already a page at that virtual
     address, then map our page there. */
  return (pagedir_get_page (t->pagedir, upage) == NULL
          && pagedir_set_page (t->pagedir, upage, kpage, writable));
}

struct thread *get_child_process (pid_t pid)
{
  struct list_elem *e;
  struct list *child_list = &thread_current()->child_list;
  struct thread *thrd;
  /* child list에 접근하여 process descriptor 검색 */
  for (e = list_begin (child_list); e != list_end (child_list); e = list_next (e))
  {
    thrd = list_entry(e, struct thread, child_elem);
    if(thrd->tid == pid) /* 해당 pid가 존재하면 process descriptor return */
      return thrd;
  }
  return NULL; /* list에 존재하지 않으면 NULL 리턴 */
}

void remove_child_process(struct thread *cp)
{
  if(cp != NULL)
	{
		list_remove(&(cp->child_elem));  /* child list에서 제거*/
		palloc_free_page(cp);           /* process descriptor 메모리 해제 */
	}
}

int process_add_file (struct file *f)
{
  int fd = thread_current()->fd_nxt;

  thread_current()->fd_table[fd] = f; /* 파일 객체를 file descriptor 테이블에 추가*/
  thread_current()->fd_nxt++; /* file descriptor의 최대값 1 증가 */

  return fd;  /* file descriptor 리턴 */
}

struct file *process_get_file(int fd)
{
  struct file *f;

  if(fd < thread_current()->fd_nxt) {
		f = thread_current()->fd_table[fd]; /* file descriptor에 해당하는 파일 객체를 리턴 */
		return f;
	}
	return NULL; /* 없을 시 NULL 리턴 */
}

void process_close_file(int fd)
{
	struct file *f;

	if((f = process_get_file(fd))) {  /* file descriptor에 해당하는 파일을 닫음 */
		file_close(f);
		thread_current()->fd_table[fd] = NULL;  /* file descriptor 테이블 해당 엔트리 초기화 */
	}
}

bool handle_mm_fault(struct vm_entry *vme)
{
  bool success = false;
  
  //void* kaddr = palloc_get_page(PAL_USER); /* palloc_get_page()를 이용해서 물리메모리 할당 */
  struct page *kpage;
  kpage = alloc_page(PAL_USER);
  kpage->vme = vme;

  switch (vme->type)
  {
  case VM_BIN:
  case VM_FILE:
    success = load_file(kpage->kaddr, vme);
    if (!success)
    {
      free_page(kpage->kaddr);
      return false;
    }
    break;
  case VM_ANON:
    swap_in(vme->swap_slot, kpage->kaddr);
    break;
  default:
    NOT_REACHED ();
  }

  // install_page를 이용해서 physical page와 virtual page 맵핑
  if (!install_page(vme->vaddr, kpage->kaddr, vme->writable))
  {
    free_page(kpage->kaddr);
    return false;
  }

  // 로드 성공 여부 반환
  vme->is_loaded = true;
  return true;
}
