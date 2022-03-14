#include "userprog/syscall.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/malloc.h"
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler(struct intr_frame *);
struct lock lock_file;

static void valid_address(void *addr, void *esp)
{
  if(addr < (void *)0x08048000 || addr >= (void *)0xc0000000) exit(-1);
  if(!find_vme(addr))
  {
    if(!verify_stack((int32_t) addr, esp)) exit(-1);
    if(!expand_stack(addr)) exit(-1);
  }
}
static void check_string(char *str, unsigned size, void *esp)
{
  while(size--) valid_address((void*)str++,esp);
}

void get_argument(void *esp, int *arg, int count)
{
  int i;
  for (i = 0; i < count; i++)
  {
    valid_address(esp + 4 * i, esp);
    arg[i] = *(int *)(esp + 4 * i);
  }
}

void syscall_init(void)
{
  intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init(&lock_file);
}

void halt(void)
{
  shutdown_power_off();
}

void exit(int status)
{
  printf("%s: exit(%d)\n", thread_name(), status);
  thread_current()->exit_status = status;
  thread_exit();
}

pid_t exec(const char *file)
{
  struct thread *child;
  pid_t pid = process_execute(file);
  if (pid == -1)
    return -1;
  child = get_child_process(pid);
  sema_down(&(child->sema_load));
  if (child->isLoad)
    return pid;
  else
    return -1;
}

int wait(pid_t pid)
{
  return process_wait(pid);
}

bool create(const char *file, unsigned initial_size)
{
  if (!file) exit(-1);
  lock_acquire(&lock_file);
  bool success = filesys_create(file, initial_size);
  lock_release(&lock_file);
  return success;
}

bool remove(const char *file)
{
  lock_acquire(&lock_file);
  bool success = filesys_remove(file);
  lock_release(&lock_file);
  return success;
}

int open(const char *file)
{
  int fd;
  struct file *f;
    
  lock_acquire(&lock_file);
  f = filesys_open(file); /* 파일을 open */

  if (!strcmp(thread_current()->name, file)) file_deny_write(f); /*ROX TEST*/

  if (f != NULL)
  {
    fd = process_add_file(f); /* 해당 파일 객체에 file descriptor 부여 */
    lock_release(&lock_file);
    return fd; /* file descriptor 리턴 */
  }
  lock_release(&lock_file);
  return -1; /* 해당 파일이 존재하지 않으면 -1 리턴 */
}

int filesize(int fd)
{
  lock_acquire(&lock_file);
  struct file *f;
  int size;
  if ((f = process_get_file(fd))) { /* file descriptor를 이용하여 파일 객체 검색 */
    size = file_length(f); /* 해당 파일의 길이를 리턴 */
  }
  else size = -1; /* 해당 파일이 존재하지 않으면 -1 리턴 */
  lock_release(&lock_file);
  return size;
}

int read(int fd, void *buffer, unsigned size)
{
  int read_size = 0;
  struct file *f;

  lock_acquire(&lock_file); /* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */

  if (fd == 0) /* file descriptor가 0일 경우(STDIN) 키보드에 입력을 버퍼에 저장 후 버퍼의 저장한 크기를 리턴 (input_getc() 이용) */
  { 
    unsigned int i;
    for (i = 0; i < size; i++)
    {
      if (((char *)buffer)[i] == '\0')
        break;
    }
    read_size = i;
  }
  else /* file descriptor가 0이 아닐 경우 파일의 데이터를 크기만큼 저장 후 읽은 바이트 수를 리턴 */
  {
    if ((f = process_get_file(fd)))
      read_size = file_read(f, buffer, size); 
  }

  lock_release(&lock_file); /* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */

  return read_size;
}

int write(int fd, const void *buffer, unsigned size)
{
  int write_size = 0;
  struct file *f;

  lock_acquire(&lock_file); /* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */

  if (fd == 1) /* file descriptor가 1일 경우(STDOUT) 버퍼에 저장된 값을 화면에 출력후 버퍼의 크기 리턴 (putbuf() 이용) */
  { 
    putbuf(buffer, size);
    write_size = size;
  }
  else /* file descriptor가 1이 아닐 경우 버퍼에 저장된 데이터를 크기만큼 파일에 기록후 기록한 바이트 수를 리턴 */
  { 
    if ((f = process_get_file(fd)))
      write_size = file_write(f, (const void *)buffer, size);
  }

  lock_release(&lock_file); /* 파일에 동시 접근이 일어날 수 있으므로 Lock 사용 */

  return write_size;
}

void seek(int fd, unsigned position)
{
  lock_acquire(&lock_file);
  struct file *f = process_get_file(fd); /* file descriptor를 이용하여 파일 객체 검색 */
  if (f != NULL) file_seek(f, position); /* 해당 열린 파일의 위치(offset)를 position만큼 이동 */
  lock_release(&lock_file);
}

unsigned
tell(int fd)
{
  lock_acquire(&lock_file);

  struct file *f = process_get_file(fd); /* file descriptor를 이용하여 파일 객체 검색 */
  unsigned pos;
  if (f != NULL) pos = file_tell(f); /* 해당 열린 파일의 위치를 return */
  else pos = 0;
  
  lock_release(&lock_file);

  return pos;
}

void close(int fd)
{
  process_close_file(fd);
}

mapid_t
mmap(int fd, void *addr)
{
  if (pg_ofs (addr) != 0 || !addr) return -1;
  if (!is_user_vaddr (addr)) return -1;

  struct mmap_file *mfe;
  size_t ofs = 0;

  //mmap_file 생성 및 초기화
  mfe = (struct mmap_file *)malloc(sizeof(struct mmap_file));
  if (!mfe) return -1;
  
  memset (mfe, 0, sizeof(struct mmap_file));
  mfe->mapid = thread_current()->mmap_nxt++;
  lock_acquire(&lock_file);
  mfe->file = file_reopen(process_get_file(fd));
  lock_release(&lock_file);

  list_init(&mfe->vme_list);
  list_push_back(&thread_current()->mmap_list, &mfe->elem);

  //vm_entry 생성 및 초기화
  int file_len = file_length(mfe->file);
  while (file_len > 0)
    {
      if (find_vme (addr)) return -1;

      size_t page_read_bytes = file_len < PGSIZE ? file_len : PGSIZE;
      size_t page_zero_bytes = PGSIZE - page_read_bytes;

      struct vm_entry *vme = make_vme(VM_FILE, addr, true, false, mfe->file, ofs, page_read_bytes, page_zero_bytes);
      if(!vme) return false;

      list_push_back(&mfe->vme_list, &vme->mmap_elem);
      insert_vme(&thread_current()->vm, vme);
      addr += PGSIZE;
      ofs += PGSIZE;
      file_len -= PGSIZE;
    }
  return mfe->mapid;
}

void munmap(mapid_t mapid)
{
  struct mmap_file *mfe = NULL;
  struct list_elem *ele;
  for (ele = list_begin(&thread_current()->mmap_list); ele != list_end(&thread_current()->mmap_list); ele = list_next (ele))
  {
    mfe = list_entry (ele, struct mmap_file, elem);
    if (mfe->mapid == mapid) break;
  }

  if (!mfe) return;
  
  for (ele = list_begin(&mfe->vme_list); ele != list_end(&mfe->vme_list);)
  {
    struct vm_entry *vme = list_entry(ele, struct vm_entry, mmap_elem);
    if (vme->is_loaded && pagedir_is_dirty(thread_current()->pagedir, vme->vaddr))
    {
      lock_acquire(&lock_file);
      if (file_write_at(vme->file, vme->vaddr, vme->read_bytes, vme->offset) != (int)vme->read_bytes) NOT_REACHED();
      lock_release(&lock_file);

      free_page(pagedir_get_page(thread_current()->pagedir,vme->vaddr));
    }
    vme->is_loaded = false;
    ele = list_remove(ele);
    delete_vme(&thread_current()->vm, vme);
  }
  list_remove(&mfe->elem);
  free(mfe);
}

static void
syscall_handler(struct intr_frame *f)
{
  
  int argv[4];
  valid_address(f->esp, f->esp);

  switch (*(uint32_t *)(f->esp))
  {
  case SYS_HALT:
    halt();
    break;
  case SYS_EXIT:
    get_argument(f->esp + 4, &argv[0], 1);
    exit((int)argv[0]);
    break;
  case SYS_EXEC:
    get_argument(f->esp + 4, &argv[0], 1);
    valid_address((void *)argv[0], f->esp);
    f->eax = exec((const char *)argv[0]);
    break;
  case SYS_WAIT:
    get_argument(f->esp + 4, &argv[0], 1);
    f->eax = wait((pid_t)argv[0]);
    break;
  case SYS_CREATE:
    get_argument(f->esp + 4, &argv[0], 2);
    valid_address((void *)argv[0], f->esp);
    f->eax = create((const char *)argv[0], (unsigned)argv[1]);
    break;
  case SYS_REMOVE:
    get_argument(f->esp + 4, &argv[0], 1);
    valid_address((void *)argv[0], f->esp);
    f->eax = remove((const char *)argv[0]);
    break;
  case SYS_OPEN:
    get_argument(f->esp + 4, &argv[0], 1);
    valid_address((void *)argv[0], f->esp);
    f->eax = open((const char *)argv[0]);
    break;
  case SYS_FILESIZE:
    get_argument(f->esp + 4, &argv[0], 1);
    f->eax = filesize(argv[0]);
    break;
  case SYS_READ:
    get_argument(f->esp + 4, &argv[0], 3);
    check_string((char*)argv[1],(unsigned)argv[2],f->esp);
    f->eax = read((int)argv[0], (void *)argv[1], (unsigned)argv[2]);
    break;
  case SYS_WRITE:
    get_argument(f->esp + 4, &argv[0], 3);
    check_string((char*)argv[1],(unsigned)argv[2],f->esp);
    f->eax = write((int)argv[0], (const void *)argv[1], (unsigned)argv[2]);
    break;
  case SYS_SEEK:
    get_argument(f->esp + 4, &argv[0], 2);
    seek(argv[0], (unsigned)argv[1]);
    break;
  case SYS_TELL:
    get_argument(f->esp + 4, &argv[0], 1);
    f->eax = tell(argv[0]);
    break;
  case SYS_CLOSE:
    get_argument(f->esp + 4, &argv[0], 1);
    close(argv[0]);
    break;
  case SYS_MMAP:
    get_argument(f->esp + 4, &argv[0], 2);
    f->eax = mmap(argv[0], (void *)argv[1]);
    break;
  case SYS_MUNMAP:
    get_argument(f->esp + 4, &argv[0], 1);
    munmap(argv[0]);
    break;
  default:
    exit(-1);
  }
}