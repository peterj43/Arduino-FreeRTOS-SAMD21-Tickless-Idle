/*
    FreeRTOS V8.0.1 - Copyright (C) 2014 Real Time Engineers Ltd.
    All rights reserved

    VISIT http://www.FreeRTOS.org TO ENSURE YOU ARE USING THE LATEST VERSION.

    ***************************************************************************
     *                                                                       *
     *    FreeRTOS provides completely free yet professionally developed,    *
     *    robust, strictly quality controlled, supported, and cross          *
     *    platform software that has become a de facto standard.             *
     *                                                                       *
     *    Help yourself get started quickly and support the FreeRTOS         *
     *    project by purchasing a FreeRTOS tutorial book, reference          *
     *    manual, or both from: http://www.FreeRTOS.org/Documentation        *
     *                                                                       *
     *    Thank you!                                                         *
     *                                                                       *
    ***************************************************************************

    This file is part of the FreeRTOS distribution.

    FreeRTOS is free software; you can redistribute it and/or modify it under
    the terms of the GNU General Public License (version 2) as published by the
    Free Software Foundation >>!AND MODIFIED BY!<< the FreeRTOS exception.

    >>!   NOTE: The modification to the GPL is included to allow you to     !<<
    >>!   distribute a combined work that includes FreeRTOS without being   !<<
    >>!   obliged to provide the source code for proprietary components     !<<
    >>!   outside of the FreeRTOS kernel.                                   !<<

    FreeRTOS is distributed in the hope that it will be useful, but WITHOUT ANY
    WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
    FOR A PARTICULAR PURPOSE.  Full license text is available from the following
    link: http://www.freertos.org/a00114.html

    1 tab == 4 spaces!

    ***************************************************************************
     *                                                                       *
     *    Having a problem?  Start by reading the FAQ "My application does   *
     *    not run, what could be wrong?"                                     *
     *                                                                       *
     *    http://www.FreeRTOS.org/FAQHelp.html                               *
     *                                                                       *
    ***************************************************************************

    http://www.FreeRTOS.org - Documentation, books, training, latest versions,
    license and Real Time Engineers Ltd. contact details.

    http://www.FreeRTOS.org/plus - A selection of FreeRTOS ecosystem products,
    including FreeRTOS+Trace - an indispensable productivity tool, a DOS
    compatible FAT file system, and our tiny thread aware UDP/IP stack.

    http://www.OpenRTOS.com - Real Time Engineers ltd license FreeRTOS to High
    Integrity Systems to sell under the OpenRTOS brand.  Low cost OpenRTOS
    licenses offer ticketed support, indemnification and middleware.

    http://www.SafeRTOS.com - High Integrity Systems also provide a safety
    engineered and independently SIL3 certified version for use in safety and
    mission critical applications that require provable dependability.

    1 tab == 4 spaces!
*/

/*-----------------------------------------------------------
 * Implementation of functions defined in portable.h for the ARM CM0 port.
 *----------------------------------------------------------*/

/* Scheduler includes. */
 /**
 * Support and FAQ: visit <a href="http://www.atmel.com/design-support/">Atmel Support</a>
 */
#include "FreeRTOS.h"
#include "task.h"

/* Constants required to manipulate the NVIC. */
#define portNVIC_SYSTICK_CTRL		( ( volatile uint32_t *) 0xe000e010 )
#define portNVIC_SYSTICK_LOAD		( ( volatile uint32_t *) 0xe000e014 )
#define portNVIC_SYSTICK_VAL		( ( volatile uint32_t *) 0xe000e018 )
#define portNVIC_INT_CTRL			( ( volatile uint32_t *) 0xe000ed04 )
#define portNVIC_SYSPRI2			( ( volatile uint32_t *) 0xe000ed20 )
#define portNVIC_SYSTICK_CLK		0x00000004
#define portNVIC_SYSTICK_INT		0x00000002
#define portNVIC_SYSTICK_ENABLE		0x00000001
#define portNVIC_PENDSVSET			0x10000000
#define portMIN_INTERRUPT_PRIORITY	( 255UL )
#define portNVIC_PENDSV_PRI			( portMIN_INTERRUPT_PRIORITY << 16UL )
#define portNVIC_SYSTICK_PRI		( portMIN_INTERRUPT_PRIORITY << 24UL )

/* Constants required to set up the initial stack. */
#define portINITIAL_XPSR			( 0x01000000 )

// Frequency of timer
#define TIMER_HERTZ								(configCPU_CLOCK_HZ)

// Value per rtos tick of timer
#define TIMER_RELOAD_VALUE_ONE_TICK				(TIMER_HERTZ / configTICK_RATE_HZ)

// One tick init for 1ms
#define TIMER_INTERVAL_TICK 					1

// Maximum value of timer
#define TIMER_MAX_COUNT							(0xFFFF)

// Maximum possible suppressed ticks with timer
#define TIMER_MAX_POSSIBLE_SUPPRESSED_TICKS		(TIMER_MAX_COUNT / TIMER_RELOAD_VALUE_ONE_TICK)



/* Let the user override the pre-loading of the initial LR with the address of
prvTaskExitError() in case is messes up unwinding of the stack in the
debugger. */
#ifdef configTASK_RETURN_ADDRESS
	#define portTASK_RETURN_ADDRESS	configTASK_RETURN_ADDRESS
#else
	#define portTASK_RETURN_ADDRESS	prvTaskExitError
#endif

/* Each task maintains its own interrupt status in the critical nesting
variable. */
static UBaseType_t uxCriticalNesting = 0x0; //orginal: 0xaaaaaaaa

/*
 * Setup the timer to generate the tick interrupts.
 */
void vPortSetupTimerInterrupt( void );

/*
 * Exception handlers.
 */
void xPortPendSVHandler( void ) __attribute__ (( naked ));
void xPortSysTickHandler( void );
void vPortSVCHandler( void );

/*
 * Start first task is a separate function so it can be tested in isolation.
 */
static void vPortStartFirstTask( void ) __attribute__ (( naked ));

/*
 * Used to catch tasks that attempt to return from their implementing function.
 */
static void prvTaskExitError( void );

/*-----------------------------------------------------------*/

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime );

/*
 * See header file for description.
 */
StackType_t *pxPortInitialiseStack( StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters )
{
	/* Simulate the stack frame as it would be created by a context switch
	interrupt. */
	pxTopOfStack--; /* Offset added to account for the way the MCU uses the stack on entry/exit of interrupts. */
	*pxTopOfStack = portINITIAL_XPSR;	/* xPSR */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) pxCode;	/* PC */
	pxTopOfStack--;
	*pxTopOfStack = ( StackType_t ) portTASK_RETURN_ADDRESS;	/* LR */
	pxTopOfStack -= 5;	/* R12, R3, R2 and R1. */
	*pxTopOfStack = ( StackType_t ) pvParameters;	/* R0 */
	pxTopOfStack -= 8; /* R11..R4. */

	return pxTopOfStack;
}
/*-----------------------------------------------------------*/

static void prvTaskExitError( void )
{
	/* A function that implements a task must not exit or attempt to return to
	its caller as there is nothing to return to.  If a task wants to exit it
	should instead call vTaskDelete( NULL ).

	Artificially force an assert() to be triggered if configASSERT() is
	defined, then stop here so application writers can catch the error. */
	configASSERT( uxCriticalNesting == ~0UL );
	portDISABLE_INTERRUPTS();
	for( ;; );
}
/*-----------------------------------------------------------*/

void vPortSVCHandler( void )
{
	/* This function is no longer used, but retained for backward
	compatibility. */
}
/*-----------------------------------------------------------*/

void vPortStartFirstTask( void )
{
	/* The MSP stack is not reset as, unlike on M3/4 parts, there is no vector
	table offset register that can be used to locate the initial stack value.
	Not all M0 parts have the application vector table at address 0. */
	__asm volatile(
	"	ldr	r2, pxCurrentTCBConst2	\n" /* Obtain location of pxCurrentTCB. */
	"	ldr r3, [r2]				\n"
	"	ldr r0, [r3]				\n" /* The first item in pxCurrentTCB is the task top of stack. */
	"	add r0, #32					\n" /* Discard everything up to r0. */
	"	msr psp, r0					\n" /* This is now the new top of stack to use in the task. */
	"	movs r0, #2					\n" /* Switch to the psp stack. */
	"	msr CONTROL, r0				\n"
	"	pop {r0-r5}					\n" /* Pop the registers that are saved automatically. */
	"	mov lr, r5					\n" /* lr is now in r5. */
	"	cpsie i						\n" /* The first task has its context and interrupts can be enabled. */
	"	pop {pc}					\n" /* Finally, pop the PC to jump to the user defined task code. */
	"								\n"
	"	.align 2					\n"
	"pxCurrentTCBConst2: .word pxCurrentTCB	  "
				  );
}
/*-----------------------------------------------------------*/

/*
 * See header file for description.
 */
void (*rtosSysTick_Handler)(void);

int sysTickHook(void) {
	if (rtosSysTick_Handler)
		rtosSysTick_Handler();
	return 0; // return zero to keep running the arduino default handler!
}

BaseType_t xPortStartScheduler( void )
{
	// rtosSysTick_Handler = &xPortSysTickHandler;
	/* Make PendSV, CallSV and SysTick the same priroity as the kernel. */
	*(portNVIC_SYSPRI2) |= portNVIC_PENDSV_PRI;
	//*(portNVIC_SYSPRI2) |= portNVIC_SYSTICK_PRI;

	/* Start the timer that generates the tick ISR.  Interrupts are disabled
	here already. */
	vPortSetupTimerInterrupt();

	/* Initialise the critical nesting count ready for the first task. */
	uxCriticalNesting = 0;

	/* Start the first task. */
	vPortStartFirstTask();

	/* Should never get here as the tasks will now be executing!  Call the task
	exit error function to prevent compiler warnings about a static function
	not being called in the case that the application writer overrides this
	functionality by defining configTASK_RETURN_ADDRESS. */
	//prvTaskExitError();

	/* Should not get here! */
	return 0;
}
/*-----------------------------------------------------------*/

void vPortEndScheduler( void )
{
	/* Not implemented in ports where there is nothing to return to.
	Artificially force an assert. */
	configASSERT( uxCriticalNesting == 1000UL );
}
/*-----------------------------------------------------------*/

void vPortYield( void )
{
	/* Set a PendSV to request a context switch. */
	*( portNVIC_INT_CTRL ) = portNVIC_PENDSVSET;

	/* Barriers are normally not required but do ensure the code is completely
	within the specified behaviour for the architecture. */
	__asm volatile( "dsb" );
	__asm volatile( "isb" );
}
/*-----------------------------------------------------------*/

void vPortEnterCritical( void )
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;
	__asm volatile(
		"dsb	\n"
		"isb	\n"
	);
}
/*-----------------------------------------------------------*/

void vPortExitCritical( void )
{
	configASSERT( uxCriticalNesting );
    uxCriticalNesting--;
    if( uxCriticalNesting == 0 )
    {
        portENABLE_INTERRUPTS();
    }
}
/*-----------------------------------------------------------*/

uint32_t ulSetInterruptMaskFromISR( void )
{
	__asm volatile(
					" mrs r0, PRIMASK	\n"
					" cpsid i			\n"
					" bx lr				  "
				  );

	/* To avoid compiler warnings.  This line will never be reached. */
	return 0;
}
/*-----------------------------------------------------------*/

void vClearInterruptMaskFromISR( uint32_t ulMask )
{
	__asm volatile(
					" msr PRIMASK, r0	\n"
					" bx lr				  "
				  );

	/* Just to avoid compiler warning. */
	( void ) ulMask;
}
/*-----------------------------------------------------------*/

void xPortPendSVHandler( void )
{
	/* This is a naked function. */

	__asm volatile
	(
	"	mrs r0, psp							\n"
	"										\n"
	"	ldr	r3, pxCurrentTCBConst			\n" /* Get the location of the current TCB. */
	"	ldr	r2, [r3]						\n"
	"										\n"
	"	sub r0, r0, #32						\n" /* Make space for the remaining low registers. */
	"	str r0, [r2]						\n" /* Save the new top of stack. */
	"	stmia r0!, {r4-r7}					\n" /* Store the low registers that are not saved automatically. */
	" 	mov r4, r8							\n" /* Store the high registers. */
	" 	mov r5, r9							\n"
	" 	mov r6, r10							\n"
	" 	mov r7, r11							\n"
	" 	stmia r0!, {r4-r7}              	\n"
	"										\n"
	"	push {r3, r14}						\n"
	"	cpsid i								\n"
	"	bl vTaskSwitchContext				\n"
	"	cpsie i								\n"
	"	pop {r2, r3}						\n" /* lr goes in r3. r2 now holds tcb pointer. */
	"										\n"
	"	ldr r1, [r2]						\n"
	"	ldr r0, [r1]						\n" /* The first item in pxCurrentTCB is the task top of stack. */
	"	add r0, r0, #16						\n" /* Move to the high registers. */
	"	ldmia r0!, {r4-r7}					\n" /* Pop the high registers. */
	" 	mov r8, r4							\n"
	" 	mov r9, r5							\n"
	" 	mov r10, r6							\n"
	" 	mov r11, r7							\n"
	"										\n"
	"	msr psp, r0							\n" /* Remember the new top of stack for the task. */
	"										\n"
	"	sub r0, r0, #32						\n" /* Go back for the low registers that are not automatically restored. */
	" 	ldmia r0!, {r4-r7}              	\n" /* Pop low registers.  */
	"										\n"
	"	bx r3								\n"
	"										\n"
	"	.align 2							\n"
	"pxCurrentTCBConst: .word pxCurrentTCB	  "
	);
}
/*-----------------------------------------------------------*/

void xPortSysTickHandler( void )
{
uint32_t ulPreviousMask;

	ulPreviousMask = portSET_INTERRUPT_MASK_FROM_ISR();
	{
		/* Increment the RTOS tick. */
		if( xTaskIncrementTick() != pdFALSE )
		{
			/* Pend a context switch. */
			*(portNVIC_INT_CTRL) = portNVIC_PENDSVSET;
		}
	}
	portCLEAR_INTERRUPT_MASK_FROM_ISR( ulPreviousMask );
}
/*-----------------------------------------------------------*/

volatile bool timer_sleep_mode = false;
volatile bool timer_sleep_mode_overflow = false;

void configure_tc_systick_timer(void) {
	TC3->COUNT16.CC[0].reg = TIMER_RELOAD_VALUE_ONE_TICK - 1; 
	while(TC3->COUNT16.STATUS.bit.SYNCBUSY);

	REG_TC3_COUNT16_COUNT = 0;
	timer_sleep_mode = false;
}

void configure_tc_sleep_timer(uint16_t counterValue) {
	

	TC3->COUNT16.CC[0].reg = counterValue > 1 ? counterValue - 1 : 1; 
	while(TC3->COUNT16.STATUS.bit.SYNCBUSY);

	REG_TC3_COUNT16_COUNT = 0;

	timer_sleep_mode = true;
	timer_sleep_mode_overflow = false;		// Prevent to not attended overflow
}

/*
 * Setup the systick timer to generate the tick interrupts at the required
 * frequency.
 */
__attribute__(( weak )) void vPortSetupTimerInterrupt( void )
{
	/* Configure SysTick to interrupt at the requested rate. */
	// *(portNVIC_SYSTICK_VAL)  = 0; /* Load the SysTick Counter Value */
	// *(portNVIC_SYSTICK_LOAD) = ( configCPU_CLOCK_HZ / configTICK_RATE_HZ ) - 1UL;
	// *(portNVIC_SYSTICK_CTRL) = portNVIC_SYSTICK_CLK | portNVIC_SYSTICK_INT | portNVIC_SYSTICK_ENABLE;

	// SYSCTRL->XOSC32K.reg |= SYSCTRL_XOSC32K_RUNSTDBY;

	GCLK->GENDIV.reg = GCLK_GENDIV_ID( 7 ) | GCLK_GENDIV_DIV(32768 / configCPU_CLOCK_HZ);

	GCLK->GENCTRL.reg = GCLK_GENCTRL_ID(7) |
						// GCLK_GENCTRL_OE |
						GCLK_GENCTRL_SRC_OSCULP32K | 
						GCLK_GENCTRL_RUNSTDBY |
						GCLK_GENCTRL_GENEN;

	while(GCLK->STATUS.bit.SYNCBUSY);

	GCLK->CLKCTRL.reg = GCLK_CLKCTRL_GEN_GCLK7 |
						GCLK_CLKCTRL_ID_TCC2_TC3 |
						GCLK_CLKCTRL_CLKEN;

	while(GCLK->STATUS.bit.SYNCBUSY);

	// PORT->Group[g_APinDescription[6].ulPort].PINCFG[g_APinDescription[6].ulPin].bit.PMUXEN = 1;
    // PORT->Group[g_APinDescription[6].ulPort].PMUX[g_APinDescription[6].ulPin >> 1].reg |= 	PORT_PMUX_PMUXO_H;


	PM->APBCMASK.bit.TC3_ = 1;

	TC3->COUNT16.CTRLA.reg = TC_CTRLA_MODE_COUNT16 | TC_CTRLA_PRESCALER_DIV1 | TC_CTRLA_WAVEGEN_MFRQ;
	while(TC3->COUNT16.STATUS.bit.SYNCBUSY);		
	

	TC3->COUNT16.CTRLA.bit.RUNSTDBY = 1;
	while(TC3->COUNT16.STATUS.bit.SYNCBUSY);

	TC3->COUNT16.INTENSET.reg = TC_INTENSET_OVF;
	
	// REG_TC3_READREQ = TC_READREQ_RCONT |            // Enable a continuous read request
                    // TC_READREQ_ADDR(0x10);        // Offset of the 16 bit COUNT register

	NVIC_SetPriority(TC3_IRQn, 3);	
	NVIC_EnableIRQ(TC3_IRQn);

	configure_tc_systick_timer();	

	TC3->COUNT16.CTRLA.bit.ENABLE = 1;
	while(TC3->COUNT16.STATUS.bit.SYNCBUSY);
}


void TC3_Handler() {
  	if (TC3->COUNT8.INTFLAG.bit.OVF && TC3->COUNT8.INTENSET.bit.OVF)             
	{
		if(!timer_sleep_mode) {
			xPortSysTickHandler();
		} else {
			timer_sleep_mode_overflow = true;
		}
		
		REG_TC3_INTFLAG = TC_INTFLAG_OVF;         // Clear the OVF interrupt flag
	}
}

void vPortSuppressTicksAndSleep( TickType_t xExpectedIdleTime ) {
	REG_TC3_INTFLAG = TC_INTFLAG_OVF; 
	
	// tc_tick_handler = NULL;
	TickType_t suppressedTicks;

	if(xExpectedIdleTime > TIMER_MAX_POSSIBLE_SUPPRESSED_TICKS) {
		suppressedTicks = TIMER_MAX_POSSIBLE_SUPPRESSED_TICKS;
	} else {
		suppressedTicks = xExpectedIdleTime;
	}

	if(eTaskConfirmSleepModeStatus() == eAbortSleep) {
		configure_tc_systick_timer();
	}
	else {
		if( xExpectedIdleTime > 0 ) {
			configure_tc_sleep_timer(suppressedTicks * TIMER_RELOAD_VALUE_ONE_TICK);
			__DSB();
			__WFI();
			
			if(timer_sleep_mode_overflow) {
				vTaskStepTick(suppressedTicks);
				timer_sleep_mode_overflow = false;
			}
			else {
				vTaskStepTick(REG_TC3_COUNT16_COUNT / TIMER_RELOAD_VALUE_ONE_TICK);
			}
		}

		configure_tc_systick_timer();
	}
}

/*-----------------------------------------------------------*/

