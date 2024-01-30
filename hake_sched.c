/* This is the only file you will be editing.
 * - hake_sched.c (Hake Scheduler Library Code)
 * - Copyright of Starter Code: Prof. Kevin Andrea, George Mason University.  All Rights Reserved
 * - Copyright of Student Code: You!
 * - Copyright of ASCII Art: [Modified from an Unknown Artist - https://www.asciiart.eu/animals/fish]
 * - Restrictions on Student Code: Do not post your code on any public site (eg. Github).
 * -- Feel free to post your code on a PRIVATE Github and give interviewers access to it.
 * -- You are liable for the protection of your code from others.
 * - Date: Aug 2023
 */

/* CS367 Project 1, Fall Semester, 2023
 * Fill in your Name, GNumber, and Section Number in the following comment fields
 * Name: Aidan Grupac
 * GNumber: G01367405
 * Section Number: CS367-004             (Replace the _ with your section number)
 */

/* hake CPU Scheduling Library
      /`·   ¸...¸
     /¸..\.´....¸`:.
 ¸.·´  ¸            `·.¸.·´)
: © ):´;   hake        ¸  {
 `·.¸ `·           ¸.·´\`·¸)
     `\\´´´´´´´´´´´\¸.·´
*/

/* Standard Library Includes */
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
/* Unix System Includes */
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <pthread.h>
#include <sched.h>
/* Local Includes */
#include "hake_sched.h"
#include "vm_support.h"
#include "vm_process.h"

/* Feel free to create any helper functions you like! */

/*** Hake Library API Functions to Complete ***/

//HELPER FUNCTION
//add any process to any queue
void add_to_queue(Hake_queue_s *queue, Hake_process_s *process){

	//if list contains no elements
	if(!queue->head) queue->head = process;

	//if process belongs at front of queue
	else if(process->pid < queue->head->pid){
		process->next = queue->head;
		queue->head = process;
	}

	//if process fits between two others or at the end of queue
	else{
		Hake_process_s *temp = queue->head;
		while(!!temp){
			//check if new pid is greater than current position and next position is null
			if(process->pid > temp->pid && !temp->next){
				process->next = temp->next;
				temp->next = process;
				break;
			}
            //check if new pid is greater than current position but less than next position
            else if(process->pid > temp->pid && process->pid < temp->next->pid){
				process->next = temp->next;
				temp->next = process;
				break;
            }
			//iterate through queue until condition is met
			else temp = temp->next;
		}
	}

	//update count
	queue->count++;
}

//HELPER FUNCTION
//search any queue for any process
Hake_process_s *search(Hake_queue_s *queue, pid_t pid){

	Hake_process_s *selected = NULL;

	//OPTION 1: queue is empty
	if(!queue->head) return NULL;

	//OPTION 2: pid is zero
	if(pid == 0){
		selected = queue->head;
		queue->head = queue->head->next;
        queue->count--;
		return selected;
	}

	//OPTION 3: pid is not zero
	if(pid != 0){

		//if pid was found at head
		if(queue->head->pid == pid){
			selected = queue->head;
			queue->head = queue->head->next;
            queue->count--;
			return selected;
		}

		//if pid was not found at head
		else{
			Hake_process_s *temp = queue->head;
            //iterate through list to find pid
			while(!!temp->next){
				if(temp->next->pid == pid){
					selected = temp->next;
					temp->next = temp->next->next;
                    queue->count--;
					return selected;
				}
                temp = temp->next;
			}
		}
	}

	//if pid is not found
	return NULL;
}

//HELPER FUNCTION
//search for first critical process in ready queue
Hake_process_s *critical_search(Hake_queue_s *queue){

    Hake_process_s *selected = NULL;

    //if head is critical
	if((queue->head->state & (1 << 27)) >> 27){
		selected = queue->head;			
        queue->head = selected->next;
		return selected;
	}

    //if head is not critical
    //search for critical in queue
	Hake_process_s *temp = queue->head;
	while(temp->next != NULL){
	    if((temp->next->state & (1 << 27)) >> 27){
	    	selected = temp->next;
	    	temp->next = temp->next->next;
	    	return selected;
		}
        temp = temp->next;
	}

    //if no critical process was found
    return NULL;
}

//HELPER FUNCTION
//search for first starving process in ready queue
Hake_process_s *starving_search(Hake_queue_s *queue){

    Hake_process_s *selected = NULL;

    //if head is starving
	if(queue->head->age >= STARVING_AGE){
		selected = queue->head;
		queue->head = selected->next;
		return selected;
	}

	//if head is not starving
    //search for starving in queue
    Hake_process_s *temp = queue->head;
	while(temp->next != NULL){
	    if(temp->next->age >= STARVING_AGE){
    		selected = temp->next;
    		temp->next = temp->next->next;
    		return selected;
    	}
        temp = temp->next;
    }

    //if no starving process was found
    return NULL;
}

//HELPER FUNCTION
//search for highest priority process in ready queue
Hake_process_s *priority_search(Hake_queue_s *queue){

    Hake_process_s *current = queue->head;
    Hake_process_s *selected = queue->head;

    //indicies of current and selected processes
	int c_index = 0;
	int s_index = 0;

    //search for index of highest priority using index of current
	while(current != NULL){
		if(current->priority < selected->priority){
			selected = current;
			s_index = c_index;
		}
		current = current->next;
		c_index++;
	}

	//if head is selected
	if(s_index == 0){
		queue->head = selected->next;
	}
	//otherwise reiterate through queue to find previous of selected process
	else{
		Hake_process_s *previous = queue->head;
		for(int i = 0; i < s_index - 1; i++){
			previous = previous->next;
		}
        //remove selected process from queue
		previous->next = previous->next->next;
	}

    return selected;
}

/* Initializes the Hake_schedule_s Struct and all of the Hake_queue_s Structs
 * Follow the project documentation for this function.
 * Returns a pointer to the new Hake_schedule_s or NULL on any error.
 */
Hake_schedule_s *hake_create() {

	//create schedule
	Hake_schedule_s *schedule = malloc(sizeof(Hake_schedule_s));
	if(!schedule) return NULL;

	//create ready queue
	schedule->ready_queue = malloc(sizeof(Hake_queue_s));
	if(!schedule->ready_queue) return NULL;
	schedule->ready_queue->count = 0;
	schedule->ready_queue->head = NULL;

	//create suspended queue
	schedule->suspended_queue = malloc(sizeof(Hake_queue_s));
	if(!schedule->suspended_queue) return NULL;
	schedule->suspended_queue->count = 0;
	schedule->suspended_queue->head = NULL;

	//create terminated queue
	schedule->terminated_queue = malloc(sizeof(Hake_queue_s));
	if(!schedule->terminated_queue) return NULL;
	schedule->terminated_queue->count = 0;
	schedule->terminated_queue->head = NULL;

	return schedule;
}

/* Allocate and Initialize a new Hake_process_s with the given information.
 * - Malloc and copy the command string, don't just assign it!
 * Follow the project documentation for this function.
 * - You may assume all arguments are Legal and Correct for this Function Only
 * Returns a pointer to the Hake_process_s on success or a NULL on any error.
 */
Hake_process_s *hake_new_process(char *command, pid_t pid, int priority, int is_critical) {

	//create process
	Hake_process_s *process = malloc(sizeof(Hake_process_s));
	if(!process) return NULL;

	//initialize fields
	process->pid = pid;
    int length = strlen(command) + 1;
	process->cmd = calloc(length, sizeof(char));
	if(!process->cmd) return NULL;
	strncpy(process->cmd, command, length);
	process->state = (is_critical) ? 0x88000000 : 0x80000000;
	process->priority = priority;
	process->age = 0;
	process->next = NULL;

	return process;
}

/* Inserts a process into the Ready Queue (singly linked list).
 * Follow the project documentation for this function.
 * - Do not create a new process to insert, insert the SAME process passed in.
 * Returns a 0 on success or a -1 on any error.
 */
int hake_insert(Hake_schedule_s *schedule, Hake_process_s *process) {

	if(!schedule || !process) return -1;

	//update state to ready
	process->state = (process->state & ~(0xF << 28)) | (1 << 31);

	//insert into ready queue
	add_to_queue(schedule->ready_queue, process);

	return 0;
}

/* Returns the number of items in a given Hake Queue (singly linked list).
 * Follow the project documentation for this function.
 * Returns the number of processes in the list or -1 on any errors.
 */
int hake_get_count(Hake_queue_s *queue) {

	if(!queue) return -1;

	return queue->count;
}

/* Selects the best process to run from the Ready Queue (singly linked list).
 * Follow the project documentation for this function.
 * Returns a pointer to the process selected or NULL if none available or on any errors.
 * - Do not create a new process to return, return a pointer to the SAME process removed.
 */
Hake_process_s *hake_select(Hake_schedule_s *schedule) {

	if(!schedule || !schedule->ready_queue->head) return NULL;

	int was_selected = 0;
	Hake_process_s *selected = NULL;
	Hake_process_s *current = NULL;

	//OPTION 1: ONE PROCESS
	if(schedule->ready_queue->count == 1){
        selected = schedule->ready_queue->head;
		schedule->ready_queue->head = NULL;
		was_selected = 1;
	}

	//OPTION 2: CRITICAL
	if(!was_selected){
		selected = critical_search(schedule->ready_queue);
        if(!!selected) was_selected = 1;
	}
    
	//OPTION 3: STARVING
	if(!was_selected){
		selected = starving_search(schedule->ready_queue);
        if(!!selected) was_selected = 1;
	}
    
	//OPTION 4: PRIORITY
	if(!was_selected){
        selected = priority_search(schedule->ready_queue);
        if(!!selected) was_selected = 1;
	}

	//after process is selected, update fields
    selected->age = 0;
	selected->next = NULL;
	schedule->ready_queue->count--;
    //set state to ready
    selected->state = (selected->state & ~(0xF << 28)) | (1 << 30);

    //increment age of remaining processes in ready queue
    current = schedule->ready_queue->head;
    while(!!current){
        current->age++;
        current = current->next;
    }

	return selected;
}

/* Move the process with matching pid from Ready to Suspended Queue.
 * Follow the specification for this function.
 * - Do not create a copy of the process in the Suspended Queue.
 * - Insert the SAME process removed from the Ready Queue to the Suspended Queue
 * Returns a 0 on success or a -1 on any error (such as process not found).
 */
int hake_suspend(Hake_schedule_s *schedule, pid_t pid) {

	if(!schedule) return -1;

	//search in ready queue
	Hake_process_s *selected = search(schedule->ready_queue, pid);
	if(!selected) return -1;

	//set state to suspended and clear next pointer
	selected->state = (selected->state & ~(0xF << 28)) | (1 << 29);
	selected->next = NULL;

	//insert into suspended queue
	add_to_queue(schedule->suspended_queue, selected);

	return 0;
}

/* Move the process with matching pid from Suspended to Ready Queue.
 * Follow the specification for this function.
 * - Do not create a copy of the process in the Ready Queue.
 * - Insert the SAME process removed from the Suspended Queue to the Ready Queue
 * Returns a 0 on success or a -1 on any error (such as process not found).
 */
int hake_resume(Hake_schedule_s *schedule, pid_t pid) {

	if(!schedule) return -1;

	//search in suspended queue
	Hake_process_s *selected = search(schedule->suspended_queue, pid);
	if(!selected) return -1;

	//set state to ready and clear next pointer
	selected->state = (selected->state & ~(0xF << 28)) | (1 << 31);
	selected->next = NULL;

	//insert into ready queue
	add_to_queue(schedule->ready_queue, selected);

	return 0;
}

/* This is called when a process exits normally that was just Running.
 * Put the given node into the Terminated Queue and set the Exit Code
 * - Do not create a new process to insert, insert the SAME process passed in.
 * Follow the project documentation for this function.
 * Returns a 0 on success or a -1 on any error.
 */
int hake_exited(Hake_schedule_s *schedule, Hake_process_s *process, int exit_code) {

	if(!schedule || !process) return -1;

	//set state to terminated and update exit code
	process->state = (process->state & ~(0xF << 28)) | ((1 << 28) + exit_code);

	//insert into terminated queue
	add_to_queue(schedule->terminated_queue, process);

	return 0;
}

/* This is called when the OS terminates a process early.
 * - This will either be in your Ready or Suspended Queue.
 * - The difference with hake_exited is that this process is in one of your Queues already.
 * Remove the process with matching pid from the Ready or Suspended Queue and add the Exit Code to it.
 * - You have to check both since it could be in either queue.
 * Follow the project documentation for this function.
 * Returns a 0 on success or a -1 on any error.
 */
int hake_terminated(Hake_schedule_s *schedule, pid_t pid, int exit_code) {

	if(!schedule) return -1;

	int was_found = 0;

	//search in ready queue
	Hake_process_s *selected = search(schedule->ready_queue, pid);
	if(!!selected) was_found = 1;

	//search in suspended queue
	if(!was_found){
		selected = search(schedule->suspended_queue, pid);
		if(!selected){
            return -1;
        }
	}

	//set state to terminated, update exit code, and clear next pointer
	selected->state = (selected->state & ~(0xF << 28)) | ((1 << 28) + exit_code);
    selected->next = NULL;

	//insert into terminated queue
	add_to_queue(schedule->terminated_queue, selected);

	return 0;
}

/* Frees all allocated memory in the Hake_schedule_s, all of the Queues, and all of their Nodes.
 * Follow the project documentation for this function.
 * Returns void.
 */
void hake_deallocate(Hake_schedule_s *schedule) {

	if(!schedule) return;

	//READY QUEUE
	Hake_process_s *current = schedule->ready_queue->head;
	while(!!current){
		Hake_process_s *temp = current;
		current = current->next;
		free(temp->cmd);
		free(temp);
	}
	free(schedule->ready_queue);

	//SUSPENDED QUEUE
	current = schedule->suspended_queue->head;
	while(!!current){
		Hake_process_s *temp = current;
		current = current->next;
		free(temp->cmd);
		free(temp);
	}
	free(schedule->suspended_queue);

	//TERMINATED QUEUE
	current = schedule->terminated_queue->head;
	while(!!current){
		Hake_process_s *temp = current;
		current = current->next;
		free(temp->cmd);
		free(temp);
	}
	free(schedule->terminated_queue);

	//free schedule
	free(schedule);
}