#include <types.h>
#include <kern/errno.h>
#include <lib.h>
#include <thread.h>
#include <addrspace.h>
#include <vm.h>
#include <machine/tlb.h>
#include <spl.h>
#include <proc.h>

/* Place your page table functions here */


void vm_bootstrap(void)
{
    /* Initialise any global components of your VM sub-system here.  
     *  
     * You may or may not need to add anything here depending what's
     * provided or required by the assignment spec.
     */
}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
    vaddr_t vaddr;
	paddr_t paddr;
	int i;
    uint32_t top_level_entry, second_level_entry;
    struct addrspace *as;
    int spl;

	switch (faulttype) {
	    case VM_FAULT_READONLY:
		/* We always create pages read-write, so we can't get this */
		return EFAULT;
	    case VM_FAULT_READ:
	    case VM_FAULT_WRITE:
		break;
	    default:
		return EINVAL;
	}

    top_level_entry = faultaddress >> 21;
    second_level_entry = (faultaddress << 11) >> 23;

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

    /* Assert that the address space has been set up properly. */
    KASSERT(as->pagetable != NULL);
	KASSERT(as->head != NULL);

    // look up in the page table

    // if we find it, load it into TLB
    if (as->pagetable[top_level_entry] != NULL && as->pagetable[top_level_entry][second_level_entry] != 0) {
        paddr = as->pagetable[top_level_entry][second_level_entry];
        spl = splhigh();
        // Page number = faultaddress & PAGE_FRAME
        tlb_random(faultaddress & PAGE_FRAME, paddr | as->loading_dirty_bit);
        splx(spl);
        return 0;
    }

    // if we don't find it, check if the region is valid
    struct region * cur = as->head;
    int writeable;
    int dirty_bit = 0;
    int find = 0;

    while (cur != NULL) {
        if (cur->vbase <= faultaddress && faultaddress < (cur->vbase + cur->npages)) {
            find = 1;
            writeable = cur->writeable;
            break;
        }
        cur = cur->next;
    }

    // if the region is invalid, return EFAULT
    if (find == 0) {
        return EFAULT;
    }

    if (writeable) {
        dirty_bit = TLBLO_DIRTY;
    }

    // allocate the frame
    vaddr = alloc_kpages(1);
    // zero fill
    bzero((void *) vaddr, PAGE_SIZE);
    // get the physical frame number
    paddr = KVADDR_TO_PADDR(vaddr);
    paddr = paddr | dirty_bit | TLBLO_VALID;

    //insert into page table
    if (as->pagetable[top_level_entry] == NULL) {
        as->pagetable[top_level_entry] = kmalloc(SECOND_LEVEL_ENTRIES_NUM * sizeof(paddr_t));
        if (as->pagetable[top_level_entry] == NULL) {
            return ENOMEM;
        }
        for (i = 0; i < SECOND_LEVEL_ENTRIES_NUM; i++) {
            as->pagetable[top_level_entry][i] = 0;
        }
    }
    as->pagetable[top_level_entry][second_level_entry] = paddr;

    // load into TLB
    spl = splhigh();
    // Page number = faultaddress & PAGE_FRAME
    tlb_random(faultaddress & PAGE_FRAME, paddr | as->loading_dirty_bit);
    splx(spl);

    return 0;
}

/*
 * SMP-specific functions.  Unused in our UNSW configuration.
 */

void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	(void)ts;
	panic("vm tried to do tlb shootdown?!\n");
}

