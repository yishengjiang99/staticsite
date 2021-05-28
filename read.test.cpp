#include "googletest"
extern "C"
{
#include "read2.c"
}
TEST(test1, test1a)
{
	readsf(fopen("file.sf2"));
}