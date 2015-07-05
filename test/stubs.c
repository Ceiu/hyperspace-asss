
/* 2>/dev/null
gcc -Wall -g -o stubs stubs.c
./stubs
#exit
rm stubs
gcc -Wall -O9 -o stubs stubs.c
./stubs
rm stubs
exit # */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;


static void * make_stub(void *arg0, void *realfunc)
{
	/* template:
	 *  58                      pop    %eax
	 *  68 aa aa aa aa          push   $0xaaaaaaaa
	 *  50                      push   %eax
	 *  e9 bb bb bb bb          jmp    0xbbbbbbbb
	 */
	u8 *stub = malloc(12);
	*(u16*)(stub+0) = 0x6858;
	*(u32*)(stub+2) = (u32)arg0;
	*(u16*)(stub+6) = 0xe950;
	*(u32*)(stub+8) = (u32)realfunc - (u32)(stub+12);
	return stub;
}


static void * make_stub_2(void *arg0, void *realfunc)
{
	static u8 template[24] =
	{
		0x50, 0x50, 0x8b, 0x44, 0x24, 0x08, 0xc7, 0x44,
		0x24, 0x08, 0xff, 0xff, 0xff, 0xff, 0x89, 0x44,
		0x24, 0x04, 0x58, 0xe9, 0xff, 0xff, 0xff, 0xff,
	};
	u8 *stub = malloc(sizeof(template));
	memcpy(stub, template, sizeof(template));
	*(u32*)(stub+10) = (u32)arg0;
	*(u32*)(stub+20) = (u32)realfunc - (u32)(stub + 24);
	return stub;
}


int foo(int arg0, int a, int b)
{
	printf("%d, %d, %d\n", arg0, a, b);
	return arg0 + a + b;
}


int main()
{
	typedef int (*func)(int a, int b);

	func f1 = make_stub((void*)100, foo);
	func f2 = make_stub_2((void*)100, foo);

	printf("%d == 400\n", f1(f1(f1(0, 0), 0), 0));

	//printf("%d == 600\n", f1(f1(0, f2(0, 0)), f2(f1(0, 0), f2(0, 0))));

	return 0;
}

