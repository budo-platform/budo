#include "core/capabilities.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(!BUDO_CAPABILITY_UDP_AVAILABLE);
    puts("web capability tests passed");
    return 0;
}