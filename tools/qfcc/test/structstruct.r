#include "test-harness.h"

typedef struct Point {
    int x;
    int y;
} Point;

typedef struct Size {
    int width;
    int height;
} Size;

typedef struct Rect {
    Point   origin;
    Size    size;
} Rect;

Rect rect = {{1, 2}, {3, 4}};

int
test_struct_1(Rect rect)
{
	return rect.origin.x;
}

int
test_struct_2(Rect rect)
{
	return rect.origin.y;
}

int
test_struct_3(Rect rect)
{
	return rect.size.width;
}

int
test_struct_4(Rect rect)
{
	return rect.size.height;
}

int
main()
{
	int ret = 0;
	ret |= test_struct_1(rect) != 1;
	ret |= test_struct_2(rect) != 2;
	ret |= test_struct_3(rect) != 3;
	ret |= test_struct_4(rect) != 4;
	return ret;
}
