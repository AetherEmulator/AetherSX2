/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2021  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "FastJmp.h"

#ifndef _WIN32

#if defined(__APPLE__)
#define PREFIX "_"
#else
#define PREFIX ""
#endif

#if defined(_M_X86_64)

asm(
	"\t.global " PREFIX "fastjmp_set\n"
	"\t.global " PREFIX "fastjmp_jmp\n"
	"\t.text\n"
	"\t" PREFIX "fastjmp_set:" R"(
	movq 0(%rsp), %rax
	movq %rsp, %rdx			# fixup stack pointer, so it doesn't include the call to fastjmp_set
	addq $8, %rdx
	movq %rax, 0(%rdi)	# actually rip
	movq %rbx, 8(%rdi)
	movq %rdx, 16(%rdi)	# actually rsp
	movq %rbp, 24(%rdi)
	movq %r12, 32(%rdi)
	movq %r13, 40(%rdi)
	movq %r14, 48(%rdi)
	movq %r15, 56(%rdi)
	xorl %eax, %eax
	ret
)"
	"\t" PREFIX "fastjmp_jmp:" R"(
	movl %esi, %eax
	movq 0(%rdi), %rdx	# actually rip
	movq 8(%rdi), %rbx
	movq 16(%rdi), %rsp	# actually rsp
	movq 24(%rdi), %rbp
	movq 32(%rdi), %r12
	movq 40(%rdi), %r13
	movq 48(%rdi), %r14
	movq 56(%rdi), %r15
	jmp *%rdx
)");

#elif defined(_M_X86_32)

asm(
	"\t.global " PREFIX "fastjmp_set\n"
	"\t.global " PREFIX "fastjmp_jmp\n"
	"\t.text\n"
	"\t" PREFIX "fastjmp_set:" R"(
	movl 0(%esp), %eax
	movl %esp, %edx			# fixup stack pointer, so it doesn't include the call to fastjmp_set
	addl $4, %edx
	movl %eax, 0(%ecx)	# actually eip
	movl %ebx, 4(%ecx)
	movl %edx, 8(%ecx)	# actually esp
	movl %ebp, 12(%ecx)
	movl %esi, 16(%ecx)
	movl %edi, 20(%ecx)
	xorl %eax, %eax
	ret
)"
	"\t" PREFIX "fastjmp_jmp:" R"(
	movl %edx, %eax
	movl 0(%ecx), %edx	# actually eip
	movl 4(%ecx), %ebx
	movl 8(%ecx), %esp	# actually esp
	movl 12(%ecx), %ebp
	movl 16(%ecx), %esi
	movl 20(%ecx), %edi
	jmp *%edx
)");

#elif defined(_M_ARM64)

asm(
	"\t.global " PREFIX "fastjmp_set\n"
	"\t.global " PREFIX "fastjmp_jmp\n"
	"\t.text\n"
	"\t" PREFIX "fastjmp_set:" R"(
	mov x16, sp
	stp x16, x30, [x0]
	stp x19, x20, [x0, #16]
	stp x21, x22, [x0, #32]
	stp x23, x24, [x0, #48]
	stp x25, x26, [x0, #64]
	stp x27, x28, [x0, #80]
	str x29, [x0, #96]
	stp d8, d9, [x0, #112]
	stp d10, d11, [x0, #128]
	stp d12, d13, [x0, #144]
	stp d14, d15, [x0, #160]
	mov w0, wzr
	br x30
)"
"\t" PREFIX "fastjmp_jmp:" R"(
	ldp x16, x30, [x0]
	mov sp, x16
	ldp x19, x20, [x0, #16]
	ldp x21, x22, [x0, #32]
	ldp x23, x24, [x0, #48]
	ldp x25, x26, [x0, #64]
	ldp x27, x28, [x0, #80]
	ldr x29, [x0, #96]
	ldp d8, d9, [x0, #112]
	ldp d10, d11, [x0, #128]
	ldp d12, d13, [x0, #144]
	ldp d14, d15, [x0, #160]
	mov w0, w1
	br x30
)");

#endif

#endif // __WIN32
