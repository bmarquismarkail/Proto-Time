#include <stdexcept>
#include "machine/plugins/abi/TimePluginAbi.h"

// Exercise a misbehaving C++ provider through the same public C ABI.
extern "C" const TimePluginModuleV1* time_get_plugin_module_v1(void);
#define TEST_IR_THROW_ON_EXECUTE 1
#include "test_ir_plugin.c"
