#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "QF/progs.h"

static int verbose = 0;

// both calculates the number of globals in the test, and ensures that both
// init and expect are the same size (will product a "void value not ignored"
// error if the sizes differ)
#define num_globals(init, expect) \
	__builtin_choose_expr ( \
		sizeof (init) == sizeof (expect), \
		(sizeof (init) / sizeof (init[0])) \
			* (sizeof (init[0]) / sizeof (pr_type_t)), \
		(void) 0\
	)

// calculate the numver of statements in the test
#define num_statements(statements) \
		(sizeof (statements) / sizeof (statements[0]))

#define BASE(b, base) (((base) & 3) << OP_##b##_SHIFT)
#define OP(a, b, c, op) ((op) | BASE(A, a) | BASE(B, b) | BASE(C, c))

typedef struct {
	const char *desc;
	pointer_t   edict_area;
	pointer_t   stack_size;
	pr_uint_t   extra_globals;
	pr_uint_t   num_globals;
	pr_uint_t   num_statements;
	dstatement_t *statements;
	pr_int_t   *init_globals;
	pr_int_t   *expect_globals;
	const char *strings;
	pr_uint_t   string_size;
} test_t;
