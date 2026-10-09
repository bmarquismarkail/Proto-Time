#include "../ScriptEngine.hpp"
#ifdef TIME_SCRIPT_QUICKJS
#include <cmath>
#include <quickjs.h>
namespace BMMQ::Script {
namespace {
struct Rejections {
  Invocation &call;
  std::array<JSValue, 64> pending{};
  std::array<bool, 64> used{};
  void close(JSContext *context) noexcept {
    for (unsigned i = 0; i < used.size(); ++i)
      if (used[i])
        JS_FreeValue(context, pending[i]);
  }
};
void promise(JSContext *context, JSValueConst value, JSValueConst,
             JS_BOOL handled, void *opaque) {
  auto &state = *static_cast<Rejections *>(opaque);
  for (unsigned i = 0; i < state.used.size(); ++i)
    if (state.used[i] &&
        JS_VALUE_GET_PTR(state.pending[i]) == JS_VALUE_GET_PTR(value)) {
      if (handled) {
        JS_FreeValue(context, state.pending[i]);
        state.used[i] = false;
      }
      return;
    }
  if (handled)
    return;
  for (unsigned i = 0; i < state.used.size(); ++i)
    if (!state.used[i]) {
      state.pending[i] = JS_DupValue(context, value);
      state.used[i] = true;
      return;
    }
  state.call.failed = true;
}
int interrupted(JSRuntime *, void *context) {
  return !static_cast<Invocation *>(context)->budget(10000);
}
bool number(JSContext *context, JSValueConst value, std::uint32_t maximum,
            std::uint16_t &out) {
  double parsed;
  if (!JS_IsNumber(value) || JS_ToFloat64(context, &parsed, value) < 0 ||
      !std::isfinite(parsed) || parsed < 0 || parsed > maximum ||
      std::floor(parsed) != parsed)
    return false;
  out = static_cast<std::uint16_t>(parsed);
  return true;
}
JSValue binding(JSContext *context, JSValueConst, int argc, JSValueConst *argv,
                int operation) {
  auto &call = *static_cast<Invocation *>(JS_GetContextOpaque(context));
  bool ok = false;
  std::uint16_t a{}, b{};
  std::uint8_t byte{};
  if ((operation == 0 || operation == 1 || operation == 4) && argc > 0 &&
      JS_IsString(argv[0])) {
    std::size_t size;
    const auto *string = JS_ToCStringLen(context, &size, argv[0]);
    if (!string)
      return JS_EXCEPTION;
    try {
      if (operation == 0)
        ok = call.getRegister({string, size}, a);
      else if (operation == 1)
        ok = argc == 2 && number(context, argv[1], 65535, b) &&
             call.setRegister({string, size}, b);
      else
        ok = call.output({string, size});
    } catch (...) {
      ok = false;
    }
    JS_FreeCString(context, string);
  } else if ((operation == 2 || operation == 3) && argc > 0 &&
             number(context, argv[0], 65535, a)) {
    if (operation == 2)
      ok = call.getByte(a, byte);
    else
      ok = argc == 2 && number(context, argv[1], 255, b) && call.setByte(a, b);
  }
  if (!ok) {
    call.failed = true;
    return JS_ThrowTypeError(context, "script binding denied or invalid");
  }
  if (operation == 0)
    return JS_NewInt32(context, a);
  if (operation == 2)
    return JS_NewInt32(context, byte);
  return JS_UNDEFINED;
}
} // namespace
Result evaluateJavaScript(std::string_view source, Invocation &call) {
  auto *runtime = JS_NewRuntime();
  if (!runtime)
    return {.error = "QuickJS initialization failed"};
  JS_SetMemoryLimit(runtime, call.limits.heapBytes);
  JS_SetMaxStackSize(runtime, 256 * 1024);
  JS_SetInterruptHandler(runtime, interrupted, &call);
  auto *context = JS_NewContext(runtime);
  if (!context) {
    JS_FreeRuntime(runtime);
    return {.error = "QuickJS heap exhausted during initialization"};
  }
  JS_SetContextOpaque(context, &call);
  Rejections rejections{call};
  JS_SetHostPromiseRejectionTracker(runtime, promise, &rejections);
  auto global = JS_GetGlobalObject(context);
  auto api = JS_NewObject(context);
  const char *names[] = {"reg", "setreg", "read8", "write8", "report"};
  bool failed = JS_IsException(api);
  for (int i = 0; i < 5 && !failed; ++i)
    failed =
        JS_SetPropertyStr(context, api, names[i],
                          JS_NewCFunctionMagic(context, binding, names[i],
                                               i == 1 || i == 3 ? 2 : 1,
                                               JS_CFUNC_generic_magic, i)) < 0;
  if (!failed)
    failed = JS_SetPropertyStr(context, global, "time", api) < 0;
  else
    JS_FreeValue(context, api);
  JS_FreeValue(context, global);
  auto value = failed ? JS_EXCEPTION
                      : JS_Eval(context, source.data(), source.size(),
                                "automation.js", JS_EVAL_TYPE_GLOBAL);
  failed = failed || JS_IsException(value);
  JS_FreeValue(context, value);
  // Promise jobs are part of the invocation, never deferred onto the guest
  // lane.
  while (!failed && JS_IsJobPending(runtime)) {
    if (!call.budget(1)) {
      failed = true;
      break;
    }
    JSContext *jobContext = nullptr;
    failed = JS_ExecutePendingJob(runtime, &jobContext) < 0;
  }
  for (bool pending : rejections.used)
    failed |= pending;
  Result result;
  if (!failed && !call.failed)
    result.success = true;
  else {
    auto error = JS_GetException(context);
    const char *message = JS_ToCString(context, error);
    result.error = message && !JS_IsNull(error) && !JS_IsUndefined(error)
                       ? message
                       : "QuickJS binding/budget or unhandled job rejection";
    JS_FreeCString(context, message);
    JS_FreeValue(context, error);
  }
  JS_SetHostPromiseRejectionTracker(runtime, nullptr, nullptr);
  rejections.close(context);
  JS_FreeContext(context);
  JS_FreeRuntime(runtime);
  return result;
}
} // namespace BMMQ::Script
#else
namespace BMMQ::Script {
Result evaluateJavaScript(std::string_view, Invocation &) {
  return {.error = "QuickJS runtime unavailable"};
}
} // namespace BMMQ::Script
#endif
