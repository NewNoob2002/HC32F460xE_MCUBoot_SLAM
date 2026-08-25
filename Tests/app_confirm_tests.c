#include <assert.h>

#include "app_confirm.h"

static int calls;
static int result;

int boot_set_confirmed(void) {
    ++calls;
    return result;
}

int main(void) {
    assert(app_confirm_running_image(false) == 0);
    assert(calls == 0);

    result = -7;
    assert(app_confirm_running_image(true) == -7);
    assert(calls == 1);
    return 0;
}
