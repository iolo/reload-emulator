#include "pico/config.h"
#include "hardware/regs/addressmap.h"
#include "hardware/regs/sio.h"

// Offsets suitable for ldr/str (must be <= 0x7c):
#define ACCUM0_OFFS     (SIO_INTERP0_ACCUM0_OFFSET    - SIO_INTERP0_ACCUM0_OFFSET)
#define ACCUM1_OFFS     (SIO_INTERP0_ACCUM1_OFFSET    - SIO_INTERP0_ACCUM0_OFFSET)
#define POP0_OFFS       (SIO_INTERP0_POP_LANE0_OFFSET - SIO_INTERP0_ACCUM0_OFFSET)

.syntax unified
.cpu cortex-m0plus
.thumb

.macro decl_func_x name
.section .scratch_x.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

.macro decl_func_y name
.section .scratch_y.\name, "ax"
.global \name
.type \name,%function
.thumb_func
\name:
.endm

#define decl_func decl_func_x

.macro decode_pixel_data rd
	ldr \rd, [r2, #POP0_OFFS]
	lsls \rd, #8
	ldr r7, [r2, #POP0_OFFS]
	orrs \rd, r7
	ldr r7, [r2, #POP0_OFFS]
	lsls r7, #24
	orrs \rd, r7
	ldr r7, [r2, #POP0_OFFS]
	lsls r7, #16
	orrs \rd, r7
.endm

// extern void apple2_render_scanline(const uint32_t *pixbuf, uint32_t *scanbuf, size_t n_pix);
decl_func apple2e_render_scanline
	push {r3-r7, lr}
	
  //@@ zoom 2x
	//@@lsls r2, #1
  //@@ zoom 4x
	lsls r2, #2
	add r2, r1
	mov ip, r2
	ldr r2, =(SIO_BASE + SIO_INTERP0_ACCUM0_OFFSET)

.align 2
1:
	ldmia r0!, {r7}
	str r7, [r2, #ACCUM0_OFFS]
	str r7, [r2, #ACCUM1_OFFS]
	decode_pixel_data r3
	decode_pixel_data r4
    //@@ zoom 2x
    //stmia r1!, {r3, r4}
    //@@ zoom 4x
    //stmia r1!, {r3}
    //stmia r1!, {r3}
    //stmia r1!, {r4}
    //stmia r1!, {r4}
    //@@ virutal scanline: pixel, blank, pixel, blank
    str r3, [r1]
    adds r1, r1, #4
    ldr r3, =0
    str r3, [r1]
    adds r1, r1, #4
    str r4, [r1]
    adds r1, r1, #4
    str r3, [r1]
    adds r1, r1, #4
	cmp r1, ip
    bne 1b

	pop {r3-r7, pc}

// extern void copy_tmdsbuf(uint32_t *dest, const uint32_t *src);
decl_func copy_tmdsbuf
	push {r2-r7, lr}

#ifdef OLIMEX_NEO6502
// 800x600, 800*3/2=1200dword, 200(repeat)*6(dward at once)=1200
.rept 200
//@@ 480x800, 480*3/2=720dword, 120(repeat)*6(dword at once)=720
//@@.rept 120
#else
.rept 200
#endif
	ldmia r1!, {r2, r3, r4, r5, r6, r7}
	stmia r0!, {r2, r3, r4, r5, r6, r7}
.endr

	pop {r2-r7, pc}
