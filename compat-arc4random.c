#include "config.h"

int unused;

#ifndef HAVE_ARC4RANDOM

#include <stdint.h>
#include <stdlib.h>

uint32_t
arc4random(void)
{
	return rand();
}

#endif /* !HAVE_ARC4RANDOM */
