

struct foo
{
	int f1;
	union
	{
		int a;
		double b;
	} u;
	int f2;
};

struct foo bar;

int main()
{
	bar.u.a;
	bar.b;
}

