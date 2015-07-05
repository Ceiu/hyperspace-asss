
#include <stdio.h>

void checkcaller(int bar, int baz);

int func1()
{
	puts("in func1");
	checkcaller(1, 4);
	puts("out of func1");
	return 0;
}

int func2()
{
	puts("in func2");
	checkcaller(1, 5);
	puts("out of func2");
	return 0;
}

void checkcaller(int bar, int baz)
{
	unsigned *i, foo;
	for (i = (unsigned*)&i; i < ((unsigned*)&i + 10); i++)
		printf("[&i+%2x]: %p\n", (char*)i - (char*)&i, *i);
	foo = *((unsigned*)&i + 2);
	/* printf("foo = %p\n", foo); */
#define CHECK(foo, func) \
	if ((char*)(foo) > (char*)(func)) { printf("called from " #func "\n"); return; }
	CHECK(foo, func2)
	CHECK(foo, func1)
#undef CHECK
}

int main()
{
	printf("func1 at %p\nfunc2 at %p\n\n", func1, func2);
	func1();
	func2();
}

