/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <current.h>
#include <mips/tlb.h>
#include <addrspace.h>
#include <vm.h>
#include <proc.h>
#include <elf.h>

/*
 * Note! If OPT_DUMBVM is set, as is the case until you start the VM
 * assignment, this file is not compiled or linked or in any way
 * used. The cheesy hack versions in dumbvm.c are used instead.
 *
 * UNSW: If you use ASST3 config as required, then this file forms
 * part of the VM subsystem.
 *
 */

struct addrspace *
as_create(void)
{
	struct addrspace *as;

	as = kmalloc(sizeof(struct addrspace));
	if (as == NULL) {
		return NULL;
	}

	/*
	 * Initialize as needed.
	 */

    // initialize pagetable
    as->pagetable = kmalloc(TOP_LEVEL_ENTRIES_NUM * sizeof(paddr_t *));
    if (as->pagetable == NULL) {
		return NULL;
	}

    int i;
    for (i = 0; i < TOP_LEVEL_ENTRIES_NUM; i++) {
        as->pagetable[i] = NULL;
    }

    // initialize rigion pointer and loading dirty bit
    as->head = NULL;
    as->loading_dirty_bit = 0;
	return as;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	struct addrspace *newas;

	newas = as_create();
	if (newas==NULL) {
		return ENOMEM;
	}

	/*
	 * Write this.
	 */
    
    // copy the page table
    int i, j;
    vaddr_t vaddr;
    paddr_t old_paddr;
    paddr_t paddr;
    int dirty_bit;

    for (i = 0; i < TOP_LEVEL_ENTRIES_NUM; i++) {
        if (old->pagetable[i] != NULL) {
            newas->pagetable[i] = kmalloc(SECOND_LEVEL_ENTRIES_NUM * sizeof(paddr_t));
            if (newas->pagetable[i] == NULL) {
                return ENOMEM;
            }
            for (j = 0; j < SECOND_LEVEL_ENTRIES_NUM; j++){
                // no mapping in this entry in old
                if (old->pagetable[i][j] == 0) {
                    newas->pagetable[i][j] = 0;
                }
                // exists a mapping in this entry in old
                else {
                    // allocate the frame
                    vaddr = alloc_kpages(1);
                    // zero fill
                    bzero((void *) vaddr, PAGE_SIZE);
                    // get the physical frame number
                    paddr = KVADDR_TO_PADDR(vaddr);
                    // copy the frame contents from old paddr to new paddr
                    old_paddr = old->pagetable[i][j] & PAGE_FRAME;
                    memcpy((void *) vaddr, (void *) PADDR_TO_KVADDR(old_paddr), PAGE_SIZE);
                    // add paddr to the new page table
                    dirty_bit = old->pagetable[i][j] & TLBLO_DIRTY;
                    paddr = paddr | dirty_bit | TLBLO_VALID;
                    newas->pagetable[i][j] = paddr;
                }
            }
        }
    }

	// copy the region linked list
	struct region *cur = old->head;
    int error;

	while (cur != NULL) {
		error = as_define_region(newas, cur->vbase, cur->npages, cur->readable, cur->writeable, cur->executable);
		if (error) {
			return error;
		}
		cur = cur->next;
	}

    // copy the loading dirty bit
	newas->loading_dirty_bit = old->loading_dirty_bit;

	*ret = newas;
	return 0;
}

void
as_destroy(struct addrspace *as)
{
	/*
	 * Clean up as needed.
	 */
    
    // clean up the page table
    int i, j;
    for (i = 0; i < TOP_LEVEL_ENTRIES_NUM; i++) {
        if (as->pagetable[i] != NULL) {
            for (j = 0; j < SECOND_LEVEL_ENTRIES_NUM; j++) {
                if (as->pagetable[i][j] != 0) {
                    free_kpages(PADDR_TO_KVADDR(as->pagetable[i][j]));
                }
            }
            kfree(as->pagetable[i]);
        }
    }
    kfree(as->pagetable);
    
    // clean up the region
    struct region *head = as->head;
    struct region *cur = as->head;
    while (cur != NULL) {
        head = head->next;
        kfree(cur);
        cur = head;
    }

	kfree(as);
}

void
as_activate(void)
{
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * Kernel thread without an address space; leave the
		 * prior address space in place.
		 */
		return;
	}

	/*
	 * Write this.
	 */

	int i, spl;

    /* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/*
	 * Write this. For many designs it won't need to actually do
	 * anything. See proc.c for an explanation of why it (might)
	 * be needed.
	 */
}

/*
 * Set up a segment at virtual address VADDR of size MEMSIZE. The
 * segment in memory extends from VADDR up to (but not including)
 * VADDR+MEMSIZE.
 *
 * The READABLE, WRITEABLE, and EXECUTABLE flags are set if read,
 * write, or execute permission should be set on the segment. At the
 * moment, these are ignored. When you write the VM system, you may
 * want to implement them.
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t memsize,
		 int readable, int writeable, int executable)
{
	/*
	 * Write this.
	 */

    // allocate mmemory for a new region
    struct region * new_region = kmalloc(sizeof(struct region));
    if (new_region == NULL) {
        return ENOMEM;
    }

    // load to new_region
    new_region->vbase = vaddr;
    new_region->npages = memsize;
    new_region->next = NULL;
    new_region->readable = readable;
    new_region->writeable = writeable;
    new_region->executable = executable;

    // load new_region to as
    if (as->head == NULL) {
        as->head = new_region;
    }
    else {
        new_region->next = as->head;
        as->head = new_region;
    }

	return 0;
}

int
as_prepare_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

    // indicate it's in a load and make it writable temporarily
	as->loading_dirty_bit = TLBLO_DIRTY;
    // flush TLB
    as_activate();
	return 0;
}

int
as_complete_load(struct addrspace *as)
{
	/*
	 * Write this.
	 */

    // recover
	as->loading_dirty_bit = 0;
    // flush TLB
    as_activate();

	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	/*
	 * Write this.
	 */
    int error;
    // USERSTACK: USERSPACETOP: MIPS_KSEG0 (the top of the user space)
    // PAGE_SIZE: 4096

    vaddr_t vaddr = USERSTACK - STACK_PAGES * PAGE_SIZE;
    size_t memsize = STACK_PAGES * PAGE_SIZE;

	error = as_define_region(as, vaddr, memsize, 0x4, 0x2, 0);
    if (error) {
        return error;
    }

	/* Initial user-level stack pointer */
	*stackptr = USERSTACK;

	return 0;
}

