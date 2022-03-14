#include <string.h>
#include "vm/page.h"
#include "vm/frame.h"
#include "vm/swap.h"
#include "threads/vaddr.h"
#include "threads/thread.h"
#include "threads/malloc.h"
#include "filesys/file.h"

static unsigned vm_hash_func(const struct hash_elem *, void *UNUSED);
static bool vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED);
static void vm_destroy_func(struct hash_elem *, void *UNUSED);

void vm_init(struct hash *vm) /* hash table 초기화 */
{
    hash_init(vm, vm_hash_func, vm_less_func, NULL); /* hash_init()으로 hash table 초기화 */
}

void vm_destroy(struct hash *vm) /* hash table 제거 */
{
    hash_destroy(vm, vm_destroy_func); /* hash_destroy()으로 hash table의 버킷리스트와 vm_entry들을 제거 */
}

struct vm_entry *find_vme(void *vaddr) /* 현재 프로세스의 주소공간에서 vaddr에 해당하는 vm_entry를 검색 */
{
    struct vm_entry vme;
    struct hash *vm = &thread_current()->vm;
    struct hash_elem *elem;
    vme.vaddr = pg_round_down(vaddr);
    if ((elem = hash_find(vm, &vme.elem))) return hash_entry(elem, struct vm_entry, elem);
    else return NULL;
}

struct vm_entry *make_vme( uint8_t type, void *vaddr, bool writable, bool is_loaded, struct file* file, 
                           size_t offset, size_t read_bytes, size_t zero_bytes)
{
    /* vm_entry 생성 (malloc 사용) */
    struct vm_entry *vme = (struct vm_entry *)malloc(sizeof(struct vm_entry));
    if (!vme) return NULL;

    /* vm_entry 멤버들 설정, virtual page가 요구될 때 읽어야할 파일의 오프셋과 사이즈, 마지막에 패딩할 제로 바이트 등등 */
    memset(vme, 0, sizeof(struct vm_entry));
    vme->type = type;
    vme->vaddr = vaddr;
    vme->writable = writable;
    vme->is_loaded = is_loaded;

    vme->file = file;
    vme->offset = offset;
    vme->read_bytes = read_bytes;
    vme->zero_bytes = zero_bytes;

    return vme;
}

bool insert_vme(struct hash *vm, struct vm_entry *vme) /* hash table에 vm_entry 삽입 */
{
    if (!hash_insert(vm, &vme->elem))
        return false;
    else
        return true;
}

bool delete_vme(struct hash *vm, struct vm_entry *vme)
{
    if (!hash_delete(vm, &vme->elem))
        return false;
    else
    {
        free_page(pagedir_get_page(thread_current()->pagedir, vme->vaddr));
        swap_free(vme->swap_slot);
        free(vme);
        return true;
    }
}

static unsigned
vm_hash_func(const struct hash_elem *e, void *aux UNUSED)
{
    struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
    return hash_int((int)vme->vaddr);
}

static bool
vm_less_func(const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
    return hash_entry(a, struct vm_entry, elem)->vaddr < hash_entry(b, struct vm_entry, elem)->vaddr;
}

static void
vm_destroy_func(struct hash_elem *e, void *aux UNUSED)
{
    struct vm_entry *vme = hash_entry(e, struct vm_entry, elem);
    free_page(pagedir_get_page(thread_current()->pagedir, vme->vaddr));
    swap_free(vme->swap_slot);
    free(vme);
}

bool load_file(void *kaddr, struct vm_entry *vme)
{
    int read_byte = file_read_at(vme->file, kaddr, vme->read_bytes, vme->offset);

    if (read_byte != (int)vme->read_bytes)
        return false;
    memset(kaddr + vme->read_bytes, 0, vme->zero_bytes);

    return true;
}

void try_to_free_pages()
{
    lock_acquire(&lru_lock);

    struct page *page = victim_page();
    bool dirty = pagedir_is_dirty(page->thread->pagedir, page->vme->vaddr);
    
    if (page->vme->type == VM_FILE)
    {
        if(dirty) file_write_at(page->vme->file, page->kaddr, page->vme->read_bytes, page->vme->offset);
    }
    else if (!(page->vme->type == VM_BIN && !dirty))
    { 
        page->vme->swap_slot = swap_out(page->kaddr);
        page->vme->type = VM_ANON;
    }

    page->vme->is_loaded = false;
    pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);
    del_page_from_lru_list(page);
    palloc_free_page(page->kaddr);
    free(page);
    lock_release(&lru_lock);
}

struct page *alloc_page(enum palloc_flags flags)
{
    struct page *page;
    page = (struct page *)malloc(sizeof(struct page));
    if (!page) return NULL;

    memset(page, 0, sizeof(struct page));
    page->thread = thread_current();
    page->kaddr = palloc_get_page(flags);
    while (!page->kaddr)
    {
        try_to_free_pages();
        page->kaddr = palloc_get_page(flags);
    }
    
    add_page_to_lru_list(page);
    return page;
}

void free_page(void *kaddr)
{
    lock_acquire(&lru_lock);
    struct page *page = find_page_in_lru_list(kaddr);
    if (page != NULL)
    {
        pagedir_clear_page(page->thread->pagedir, page->vme->vaddr);
        del_page_from_lru_list(page);
        palloc_free_page(page->kaddr);
        free(page);
    }
    lock_release(&lru_lock);
}
