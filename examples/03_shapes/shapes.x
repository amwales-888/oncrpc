/*
 * shapes.x - enums, structs and discriminated unions.
 *
 * A union carries one of several values; the discriminant ("kind", "ok")
 * says which. In C it becomes a struct with the discriminant and a union
 * called <name>_u holding the arms.
 */
enum shape_kind {
    CIRCLE    = 1,
    RECTANGLE = 2,
    TRIANGLE  = 3
};

struct rectangle {
    double width;
    double height;
};

struct triangle {
    double a;
    double b;
    double c;
};

union shape switch (shape_kind kind) {
case CIRCLE:
    double radius;
case RECTANGLE:
    rectangle rect;
case TRIANGLE:
    triangle tri;
};

/* Either an area or an explanation of why there is none. */
union area_result switch (bool ok) {
case TRUE:
    double area;
case FALSE:
    string error<100>;
};

program SHAPES_PROG {
    version SHAPES_VERS {
        area_result AREA(shape) = 1;
    } = 1;
} = 0x20000103;
