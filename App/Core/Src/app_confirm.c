#include "app_confirm.h"

#include "bootutil/bootutil_public.h"

int app_confirm_running_image(bool auto_confirm) {
    return auto_confirm ? boot_set_confirmed() : 0;
}
