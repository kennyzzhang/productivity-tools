#include <cilk/cilk.h>
#include <string.h>

int main()
{
	strdup("a");
	return 0;
}
