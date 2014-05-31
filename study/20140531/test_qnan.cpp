#include <cstdio>
#include <float.h>

//About QNAN
//reference : http://blogs.msdn.com/b/oldnewthing/archive/2010/03/05/9973225.aspx
//            http://cocoa106.tistory.com/7
int main(void)
{
	//double a,b=2,c=1.0e-101;
	float a,b=2,c=1.0e-101;
	a = b/c;

	if(c == 0.0)
	{
		printf("matched!!\n");
	}

	printf("%.30f : %x :  %.30f\n",a,a,c);

	printf("%e : %e : %e\n",FLT_EPSILON, FLT_MIN, DBL_MIN);
}
