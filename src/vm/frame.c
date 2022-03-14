#include "vm/frame.h"
#include <list.h>
#include "vm/page.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"

void lru_list_init(void)
{
  list_init(&lru_list);
  lock_init(&lru_lock);
  lru_clock = NULL;
}

void add_page_to_lru_list(struct page *page)
{
  lock_acquire(&lru_lock);
  list_push_back(&lru_list, &page->lru_elem);
  lock_release(&lru_lock);
}

void del_page_from_lru_list(struct page *page)
{
  if (lru_clock == &page->lru_elem) lru_clock = list_remove(lru_clock);
  else list_remove(&page->lru_elem);
}

static struct list_elem *get_next_lru_clock()
{
  if (!lru_clock || lru_clock == list_end(&lru_list))
  {
    if (!list_empty(&lru_list)) return (lru_clock = list_begin(&lru_list));
    else return NULL;      
  } 
  else
  {
    lru_clock = list_next(lru_clock);
    if (lru_clock == list_end(&lru_list)) return get_next_lru_clock();
    else return lru_clock;
  }

}

struct page *find_page_in_lru_list(void *kaddr)
{
  struct list_elem *ele;
  for (ele = list_begin(&lru_list); ele != list_end(&lru_list); ele = list_next(ele))
  {
    struct page *page = list_entry(ele, struct page, lru_elem);
    if (page->kaddr == kaddr)
      return page;
  }
  return NULL;
}

struct page *victim_page()
{
  struct list_elem *ele = get_next_lru_clock();
  struct page *page = list_entry(ele, struct page, lru_elem);
  while (pagedir_is_accessed(page->thread->pagedir, page->vme->vaddr))
  {
    pagedir_set_accessed(page->thread->pagedir, page->vme->vaddr, false);
    ele = get_next_lru_clock();
    page = list_entry(ele, struct page, lru_elem);
  }
  return page;
}