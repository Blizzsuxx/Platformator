#include "box2d/base.h"

#include <stdio.h>
#include <stdlib.h>

int DynamicTreeTest(void);

int TestAssertFcn(const char *condition, const char *fileName, int lineNumber)
{
    fprintf(stderr, "Box2D assertion: %s at %s:%d\n", condition, fileName, lineNumber);
    abort();
}

void TestLogFcn(const char *message)
{
    puts(message);
}

int main(void)
{
    b2SetAssertCallback(TestAssertFcn);
    b2SetLogCallback(TestLogFcn);
    return DynamicTreeTest();
}
