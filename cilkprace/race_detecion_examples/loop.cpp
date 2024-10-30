#include <iostream>
#include <cilk/cilk.h>

int main()
{
	int j = 0;
	cilk_for (int i = 1; i <= 2; i++)
	{
		for (int z = 0; z <= i; z++)
		  ;//j += 1;
	}
	std::cout << "J: " << j << std::endl;
}
