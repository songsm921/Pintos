#ifndef VM_PAGE_H
#define VM_PAGE_H

#define VM_BIN 0
#define VM_FILE 1
#define VM_ANON 2

#include <hash.h>
#include "userprog/syscall.h"
#include "threads/palloc.h"
#include "filesys/off_t.h"
extern struct lock lru_lock;

struct vm_entry {
    uint8_t type; /* VM_BIN, VM_FILE, VM_ANON의 타입 */
    void *vaddr; /* virtual page number */
    bool writable; /* 해당 주소에 write 가능 여부 */
    bool is_loaded; /* physical memory의 load 여부를 알려주는 flag */
    struct file* file; /* mapping된 파일 */
    size_t offset; /* read 할 파일 offset */
    size_t read_bytes; /* virtual page에 쓰여져 있는 데이터 byte 수 */
    size_t zero_bytes; /* 0으로 채울 남은 페이지의 byte 수 */

    struct hash_elem elem; /* Hash Table Element */
    struct list_elem mmap_elem; /* mmap 리스트 element */
    size_t swap_slot;
};

struct mmap_file {
    mapid_t mapid;
    struct file* file;
    struct list_elem elem;
    struct list vme_list;
};

struct page {
    void *kaddr;
    struct vm_entry *vme;
    struct thread *thread;
    struct list_elem lru_elem;
};


void vm_init(struct hash *vm);  /* hash table 초기화 */
void vm_destroy(struct hash *vm);   /* hash table 제거 */
struct vm_entry *find_vme(void *vaddr); /* 현재 프로세스의 주소공간에서 vaddr에 해당하는 vm_entry를 검색 */
struct vm_entry *make_vme( uint8_t type, void *vaddr, bool writable, bool is_loaded, struct file* file, 
                           size_t offset, size_t read_bytes, size_t zero_bytes);
bool insert_vme(struct hash *vm, struct vm_entry *vme); /* hash table에 vm_entry 삽입 */
bool delete_vme(struct hash *vm, struct vm_entry *vme); /* 해시 테이블에서 vm_entry삭제 */
bool load_file(void *kaddr, struct vm_entry *vme);

void try_to_free_pages();
struct page* alloc_page(enum palloc_flags flags);
void free_page(void *kaddr);

#endif