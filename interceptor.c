#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <asm/current.h>
#include <asm/ptrace.h>
#include <linux/sched.h>
#include <linux/cred.h>
#include <asm/unistd.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <linux/syscalls.h>
#include "interceptor.h"


MODULE_DESCRIPTION("My kernel module");
MODULE_AUTHOR("Me");
MODULE_LICENSE("GPL");

//----- System Call Table Stuff ------------------------------------
/* Symbol that allows access to the kernel system call table */
extern void* sys_call_table[];

/* The sys_call_table is read-only => must make it RW before replacing a syscall */
void set_addr_rw(unsigned long addr) {

	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);

	if (pte->pte &~ _PAGE_RW) pte->pte |= _PAGE_RW;

}

/* Restores the sys_call_table as read-only */
void set_addr_ro(unsigned long addr) {

	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);

	pte->pte = pte->pte &~_PAGE_RW;

}
//-------------------------------------------------------------


//----- Data structures and bookkeeping -----------------------
/**
 * This block contains the data structures needed for keeping track of
 * intercepted system calls (including their original calls), pid monitoring
 * synchronization on shared data, etc.
 * It's highly unlikely that you will need any globals other than these.
 */

/* List structure - each intercepted syscall may have a list of monitored pids */
struct pid_list {
	pid_t pid;
	struct list_head list;
};


/* Store info about intercepted/replaced system calls */
typedef struct {

	/* Original system call */
	asmlinkage long (*f)(struct pt_regs);

	/* Status: 1=intercepted, 0=not intercepted */
	int intercepted;

	/* Are any PIDs being monitored for this syscall? */
	int monitored;
	/* List of monitored PIDs */
	int listcount;
	struct list_head my_list;
}mytable;

/* An entry for each system call */
mytable table[NR_syscalls+1];

/* Access to the table and pid lists must be synchronized */
spinlock_t pidlist_lock = SPIN_LOCK_UNLOCKED;
spinlock_t calltable_lock = SPIN_LOCK_UNLOCKED;
//-------------------------------------------------------------


//----------LIST OPERATIONS------------------------------------
/**
 * These operations are meant for manipulating the list of pids
 * Nothing to do here, but please make sure to read over these functions
 * to understand their purpose, as you will need to use them!
 */

/**
 * Add a pid to a syscall's list of monitored pids.
 * Returns -ENOMEM if the operation is unsuccessful.
 */
static int add_pid_sysc(pid_t pid, int sysc)
{
	struct pid_list *ple = (struct pid_list*) kmalloc(sizeof(struct pid_list), \
														GFP_KERNEL);

	if (!ple)
		return -ENOMEM;

	INIT_LIST_HEAD(&ple->list);
	ple->pid=pid;

	list_add(&ple->list, &(table[sysc].my_list));
	table[sysc].listcount++;

	return 0;
}

/**
 * Remove a pid from a system call's list of monitored pids.
 * Returns -EINVAL if no such pid was found in the list.
 */
static int del_pid_sysc(pid_t pid, int sysc)
{
	struct list_head *i;
	struct pid_list *ple;

	list_for_each(i, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		if(ple->pid == pid) {

			list_del(i);
			kfree(ple);

			table[sysc].listcount--;
			/* If there are no more pids in sysc's list of pids, then
			 * stop the monitoring only if it's not for all pids
			 * (monitored=2) */
			if(table[sysc].listcount == 0 && table[sysc].monitored == 1) {
				table[sysc].monitored = 0;
			}

			return 0;
		}
	}

	return -EINVAL;
}

/**
 * Remove a pid from all the lists of monitored pids
 * (for all intercepted syscalls).
 * Returns -1 if this process is not being monitored in any list.
 */
static int del_pid(pid_t pid)
{
	struct list_head *i, *n;
	struct pid_list *ple;
	int ispid = 0, s = 0;

	for(s = 1; s < NR_syscalls; s++) {

		list_for_each_safe(i, n, &(table[s].my_list)) {

			ple=list_entry(i, struct pid_list, list);
			if(ple->pid == pid) {

				list_del(i);
				ispid = 1;
				kfree(ple);

				table[s].listcount--;
				/* If there are no more pids in sysc's list of pids, then
				 * stop the monitoring only if it's not for all pids
				 * (monitored=2) */
				if(table[s].listcount == 0 && table[s].monitored == 1) {
					table[s].monitored = 0;
				}
			}
		}
	}

	if (ispid) return 0;
	return -1;
}

/**
 * Clear the list of monitored pids for a specific syscall.
 */
static void destroy_list(int sysc) {

	struct list_head *i, *n;
	struct pid_list *ple;

	list_for_each_safe(i, n, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		list_del(i);
		kfree(ple);
	}

	table[sysc].listcount = 0;
	table[sysc].monitored = 0;
}

/**
 * Check if two pids have the same owner - useful for checking if a pid
 * requested to be monitored is owned by the requesting process.
 * Remember that when requesting to start monitoring for a pid, only the
 * owner of that pid is allowed to request that.
 */
static int check_pid_from_list(pid_t pid1, pid_t pid2) {

	struct task_struct *p1 = pid_task(find_vpid(pid1), PIDTYPE_PID);
	struct task_struct *p2 = pid_task(find_vpid(pid2), PIDTYPE_PID);
	if(p1->real_cred->uid != p2->real_cred->uid)
		return -EPERM;
	return 0;
}

/**
 * Check if a pid is already being monitored for a specific syscall.
 * Returns 1 if it already is, or 0 if pid is not in sysc's list.
 */
static int check_pid_monitored(int sysc, pid_t pid) {

	struct list_head *i;
	struct pid_list *ple;

	list_for_each(i, &(table[sysc].my_list)) {

		ple=list_entry(i, struct pid_list, list);
		if(ple->pid == pid)
			return 1;

	}
	return 0;
}
//----------------------------------------------------------------

//----- Intercepting exit_group ----------------------------------
/**
 * Since a process can exit without its owner specifically requesting
 * to stop monitoring it, we must intercept the exit_group system call
 * so that we can remove the exiting process's pid from *all* syscall lists.
 */

/**
 * Stores original exit_group function - after all, we must restore it
 * when our kernel module exits.
 */
void (*orig_exit_group)(int);

/**
 * Our custom exit_group system call.
 *
 * TODO: When a process exits, make sure to remove that pid from all lists.
 * The exiting process's PID can be retrieved using the current variable (current->pid).
 * Don't forget to call the original exit_group.
 */
void my_exit_group(int status) {
	pid_t current_pid = current->pid;

	del_pid(current_pid);
	orig_exit_group(status);
}
//----------------------------------------------------------------



/**
 * This is the generic interceptor function.
 * It should just log a message and call the original syscall.
 *
 * TODO: Implement this function.
 * - Check first to see if the syscall is being monitored for the current->pid.
 * - Recall the convention for the "monitored" flag in the mytable struct:
 *     monitored=0 => not monitored
 *     monitored=1 => some pids are monitored, check the corresponding my_list
 *     monitored=2 => all pids are monitored for this syscall
 * - Use the log_message macro, to log the system call parameters!
 *     Remember that the parameters are passed in the pt_regs registers.
 *     The syscall parameters are found (in order) in the
 *     ax, bx, cx, dx, si, di, and bp registers (see the pt_regs struct).
 * - Don't forget to call the original system call, so we allow processes to proceed as normal.
 */
asmlinkage long interceptor(struct pt_regs reg) {
	pid_t cur_pid;
	unsigned long syscall;
	int monitored;

	cur_pid = current->pid;
	syscall = reg.ax;

	if ((monitored = check_pid_monitored(syscall, cur_pid)) == 0)
		printk("WARNING!!! This syscall is not monitoring the given PID\n");
	else if (monitored == 1)
		log_message(cur_pid, syscall, reg.bx, reg.cx, reg.dx, reg.si, reg.di, reg.bp);

	return table[syscall].f(reg);  //Calls the original system call
}

/**
 * My system call - this function is called whenever a user issues a MY_CUSTOM_SYSCALL system call.
 * When that happens, the parameters for this system call indicate one of 4 actions/commands:
 *      - REQUEST_SYSCALL_INTERCEPT to intercept the 'syscall' argument
 *      - REQUEST_SYSCALL_RELEASE to de-intercept the 'syscall' argument
 *      - REQUEST_START_MONITORING to start monitoring for 'pid' whenever it issues 'syscall'
 *      - REQUEST_STOP_MONITORING to stop monitoring for 'pid'
 *      For the last two, if pid=0, that translates to "all pids".
 *
 * TODO: Implement this function, to handle all 4 commands correctly.
 *
 * 1 - For each of the commands, check that the arguments are valid (-EINVAL):
 *   a) the syscall must be valid (not negative, not > NR_syscalls, and not MY_CUSTOM_SYSCALL itself)
 *   b) the pid must be valid for the last two commands. It cannot be a negative integer,
 *      and it must be an existing pid (except for the case when it's 0, indicating that we want
 *      to start/stop monitoring for "all pids").
 *      If a pid belongs to a valid process, then the following expression is non-NULL:
 *           pid_task(find_vpid(pid), PIDTYPE_PID)
 * 2 - Check that the caller has the right permissions (-EPERM)
 *      For the first two commands, we must be root (see the current_uid() macro).
 *      For the last two commands, the following logic applies:
 *        - is the calling process root? if so, all is good, no doubts about permissions.
 *        - if not, then check if the 'pid' requested is owned by the calling process
 *        - also, if 'pid' is 0 and the calling process is not root, then access is denied
 *          (monitoring all pids is allowed only for root, obviously).
 *      To determine if two pids have the same owner, use the helper function provided above in this file.
 * 3 - Check for correct context of commands (-EINVAL):
 *     a) Cannot de-intercept a system call that has not been intercepted yet.
 *     b) Cannot stop monitoring for a pid that is not being monitored, or if the
 *        system call has not been intercepted yet.
 * 4 - Check for -EBUSY conditions:
 *     a) If intercepting a system call that is already intercepted.
 *     b) If monitoring a pid that is already being monitored.
 * 5 - If a pid cannot be added to a monitored list, due to no memory being available,
 *   an -ENOMEM error code should be returned.
 *
 * 6 - Make sure to keep track of all the metadata on what is being intercepted and monitored.
 *   Use the helper functions provided above for dealing with list operations.
 *
 * 7 - Whenever altering the sys_call_table, make sure to use the set_addr_rw/set_addr_ro functions
 *   to make the system call table writable, then set it back to read-only.
 *   For example: set_addr_rw((unsigned long)sys_call_table);
 *   Also, make sure to save the original system call (you'll need it for 'interceptor' to work correctly).
 *
 * 8 - Make sure to use synchronization to ensure consistency of shared data structures.
 *   Use the calltable_spinlock and pidlist_spinlock to ensure mutual exclusion for accesses
 *   to the system call table and the lists of monitored pids. Be careful to unlock any spinlocks
 *   you might be holding, before you exit the function (including error cases!).
 */
asmlinkage long my_syscall(int cmd, int syscall, int pid) {

	int current_uid = current_uid();

	// 1 a)
	if (syscall < 0){
		return -EINVAL;
	} else if (syscall > NR_syscalls){
		return -EINVAL;
	} else if (syscall == MY_CUSTOM_SYSCALL){
		return -EINVAL;
	}

	if (cmd == REQUEST_SYSCALL_INTERCEPT){
		// Check if we are root
		if (current_uid != 0){
			return -EPERM;
		}

		// Check if trying to intercept a syscall twice
		if (table[syscall].intercepted == 1){
			return -EBUSY;
		}

		// Set the intercepted flag to 1
		spin_lock(&calltable_lock);
		table[syscall].intercepted = 1;

		/**
		* Replace the orignal system call with your inter function, so that
		* whenever a process calls that system call, the kernel will execute
		* our interceptor function instead of the original function
		*/
		set_addr_rw((unsigned long) sys_call_table);
		sys_call_table[syscall] = interceptor;
		set_addr_ro((unsigned long) sys_call_table);

		spin_unlock(&calltable_lock);


	} else if (cmd == REQUEST_SYSCALL_RELEASE){
		// Check if we are root
		if (current_uid != 0){
			return -EPERM;
		}

		// Check if trying to de-intercept a syscall that has not been
		// intercepted yet
		if (table[syscall].intercepted == 0){
			return -EINVAL;
		}

		// Set the intercepted flag to 0
		spin_lock(&calltable_lock);
		table[syscall].intercepted = 0;

		// Restore it with the original system call in the table corresponding
		// to its posiiton.
		set_addr_rw((unsigned long) sys_call_table);
		sys_call_table[syscall] = table[syscall].f;
		set_addr_ro((unsigned long) sys_call_table);
		destroy_list(syscall);

		spin_unlock(&calltable_lock);


	} else if (cmd == REQUEST_START_MONITORING){
		// Check if pid is valid
		if (pid < 0){
			return -EINVAL;
		} else if (pid_task(find_vpid(pid), PIDTYPE_PID) == NULL && pid != 0){
			return -EINVAL;
		}

		// Check if we have right permissions
		if (current_uid != 0){
			if (pid == 0){
				return -EPERM;
			} else if (check_pid_from_list(pid, current->pid) != 0){
				return -EPERM;
			}
		}
		//check if the syscall is intercepted before monitoring.
		if (table[syscall].intercepted == 0){
			printk("Trying to start monitoring a non-intercepted syscall\n");
			return -EINVAL;
		}
		// Check if trying to monitor a pid that is already being monitored
		if (check_pid_monitored(syscall, pid) == 1){
			return -EBUSY;
		}

		// Here we are sure that calling process is the root ~ no permissons.
		// and pid passed in is valid.
		spin_lock(&pidlist_lock);

		if (pid == 0){
			// All pids are monitored.
			table[syscall].monitored = 2;
		} else {

			table[syscall].monitored = 1;
			// Check if there is enough memory to add pid to the monitored list
			// If yes then add it.
			if (add_pid_sysc(pid, syscall)!= 0){
				return -ENOMEM;
			}
		}

		spin_unlock(&pidlist_lock);

	} else if (cmd == REQUEST_STOP_MONITORING){
		// Check if pid is valid
		if (pid < 0){
			printk("Negative pid!\n");
			return -EINVAL;
		} else if (pid_task(find_vpid(pid), PIDTYPE_PID) == NULL && pid != 0){
			printk("This pid does not belong to a valid process\n");
			return -EINVAL;
		}

		// Check if we have right permissions
		if (current_uid != 0){
			if (pid == 0){
				printk("Monitoring all pids is only allowed for root\n");
				return -EPERM;
			} else if (check_pid_from_list(pid, current->pid) != 0){
				printk("Not right permission\n");
				return -EPERM;
			}
		}

		// Check for correct context of commands
		if (table[syscall].intercepted == 0){
			printk("Trying to stop monitoring a non-intercepted syscall\n");
			return -EINVAL;
		}
		if (pid != 0 && check_pid_monitored(syscall, pid) == 0){
			printk("This pid is not being monitored!\n");
			return -EINVAL;
		}

		spin_lock(&pidlist_lock);
		if (pid == 0)
			// We wan't to de-monitor all the pids.
			destroy_list(syscall);
		else
			del_pid_sysc(pid, syscall);
		spin_unlock(&pidlist_lock);

	} else {
		printk("Not using a valid command\n");
		return -EINVAL;
	}

	return 0;
}

/**
 *
 */
long (*orig_custom_syscall)(void);


/**
 * Module initialization.
 *
 * TODO: Make sure to:
 * - Hijack MY_CUSTOM_SYSCALL and save the original in orig_custom_syscall.
 * - Hijack the exit_group system call (__NR_exit_group) and save the original
 *   in orig_exit_group.
 * - Make sure to set the system call table to writable when making changes,
 *   then set it back to read only once done.
 * - Perform any necessary initializations for bookkeeping data structures.
 *   To initialize a list, use
 *        INIT_LIST_HEAD (&some_list);
 *   where some_list is a "struct list_head".
 * - Ensure synchronization as needed.
 */
static int init_function(void) {
	int system_call;

	/**
	 * We will use synchronization here because if any system call tries to
	 * intercept/modify sys_call_table when we try to unload the module can
	 * lead to one system call broken.
	 */
	spin_lock(&calltable_lock);

	// Saving the original MY_CUSTOM_SYSCALL
	orig_custom_syscall = sys_call_table[MY_CUSTOM_SYSCALL];

 	// Make sys_call_table writable and then change it to readable when done.
  	set_addr_rw((unsigned long)sys_call_table);
	sys_call_table[MY_CUSTOM_SYSCALL] = my_syscall;   //Replacing MY_CUSTOM_SYSCALL with my_syscall
	set_addr_ro((unsigned long)sys_call_table);

	// Hijacking the exit_group system call
	// Save the original exit group
	orig_exit_group = sys_call_table[__NR_exit_group];

	// Replace it with the our custom exit group
	set_addr_rw((unsigned long)sys_call_table);
	sys_call_table[__NR_exit_group] = my_exit_group;
	set_addr_ro((unsigned long)sys_call_table);

	spin_unlock(&calltable_lock);

	// Initializing every system_call in the table.
	/**
	* Every system_call in the table are the function pointers of all the
	* system calls in sys_call_table. Initializing the struct and list of all
	* the system calls in the table.
	*/
	for (system_call = 0; system_call <= NR_syscalls; system_call++){
			table[system_call].f = sys_call_table[system_call];
			table[system_call].intercepted = 0;
			table[system_call].monitored = 0;
			INIT_LIST_HEAD(&table[system_call].my_list);
		}

	return 0;
}

/**
 * Module exits.
 *
 * TODO: Make sure to:
 * - Restore MY_CUSTOM_SYSCALL to the original syscall.
 * - Restore __NR_exit_group to its original syscall.
 * - Make sure to set the system call table to writable when making changes,
 *   then set it back to read only once done.
 * - Ensure synchronization, if needed.
 */
static void exit_function(void) {

	spin_lock(&calltable_lock);

 	// Restoring the original MY_CUSTOM_SYSCALL
  	set_addr_rw((unsigned long)sys_call_table);
	sys_call_table[MY_CUSTOM_SYSCALL] = orig_custom_syscall;
	set_addr_ro((unsigned long)sys_call_table);

	// Restoring the original exit group
	set_addr_rw((unsigned long)sys_call_table);
	sys_call_table[__NR_exit_group] = orig_exit_group;
	set_addr_ro((unsigned long)sys_call_table);

	spin_unlock(&calltable_lock);

}

module_init(init_function);
module_exit(exit_function);
