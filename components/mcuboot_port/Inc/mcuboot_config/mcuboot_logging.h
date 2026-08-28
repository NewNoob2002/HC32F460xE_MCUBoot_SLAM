#ifndef MCUBOOT_LOGGING_H
#define MCUBOOT_LOGGING_H

#if defined(HC32_DEBUG_LOG)
#include "elog.h"
#define MCUBOOT_LOG_ERR(...) elog_e("mcuboot", __VA_ARGS__)
#define MCUBOOT_LOG_WRN(...) elog_w("mcuboot", __VA_ARGS__)
#define MCUBOOT_LOG_INF(...) elog_i("mcuboot", __VA_ARGS__)
#define MCUBOOT_LOG_DBG(...) elog_d("mcuboot", __VA_ARGS__)
#else
#define MCUBOOT_LOG_ERR(...) do { } while (0)
#define MCUBOOT_LOG_WRN(...) do { } while (0)
#define MCUBOOT_LOG_INF(...) do { } while (0)
#define MCUBOOT_LOG_DBG(...) do { } while (0)
#endif
#define MCUBOOT_LOG_SIM(...) do { } while (0)
#define MCUBOOT_LOG_MODULE_DECLARE(module)
#define MCUBOOT_LOG_MODULE_REGISTER(module)

#endif
