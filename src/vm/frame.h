#ifndef VM_FRAME_H
#define VM_FRAME_H

#include <list.h>
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"

struct list lru_list;
struct lock lru_lock;
struct list_elem *lru_clock;

static struct list_elem *get_next_lru_clock();

void lru_list_init(void);
void add_page_to_lru_list(struct page *page);
void del_page_from_lru_list(struct page *page);
struct page *find_page_in_lru_list(void *kaddr);
void try_to_free_pages();
struct page *victim_page();


#endif