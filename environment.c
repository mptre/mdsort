#include "environment.h"

#include "config.h"

#include <string.h>

void
environment_init(struct environment *env)
{
	memset(env, 0, sizeof(*env));
}
