#include "../ScriptEngine.hpp"
#ifdef TIME_SCRIPT_LUA
extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}
#include <cstdlib>
namespace BMMQ::Script {
namespace {
struct Heap {
  std::size_t bytes{}, maximum{};
};
void *allocate(void *context, void *ptr, std::size_t old, std::size_t size) {
  auto &heap = *static_cast<Heap *>(context);
  if (!ptr)
    old = 0;
  if (!size) {
    std::free(ptr);
    heap.bytes -= old;
    return nullptr;
  }
  if (size > heap.maximum - (heap.bytes - old))
    return nullptr;
  auto *next = std::realloc(ptr, size);
  if (next)
    heap.bytes = heap.bytes - old + size;
  return next;
}
Invocation &invocation(lua_State *state) {
  return **static_cast<Invocation **>(lua_getextraspace(state));
}
void hook(lua_State *state, lua_Debug *) {
  if (!invocation(state).budget(1))
    luaL_error(state, "script instruction/time budget exhausted");
}
int reg(lua_State *state) {
  auto &call = invocation(state);
  const bool failed = call.failed;
  call.failed = true;
  std::size_t size;
  const char *name = luaL_checklstring(state, 1, &size);
  std::uint16_t value;
  if (!invocation(state).getRegister({name, size}, value))
    return luaL_error(state, "unknown register");
  call.failed = failed;
  lua_pushinteger(state, value);
  return 1;
}
int setreg(lua_State *state) {
  auto &call = invocation(state);
  const bool failed = call.failed;
  call.failed = true;
  std::size_t size;
  const char *name = luaL_checklstring(state, 1, &size);
  const auto value = luaL_checkinteger(state, 2);
  if (value < 0 || value > 65535 ||
      !invocation(state).setRegister({name, size}, value))
    return luaL_error(state, "register mutation denied or invalid");
  call.failed = failed;
  return 0;
}
int read(lua_State *state) {
  auto &call = invocation(state);
  const bool failed = call.failed;
  call.failed = true;
  const auto address = luaL_checkinteger(state, 1);
  std::uint8_t value;
  if (address < 0 || address > 65535 ||
      !invocation(state).getByte(address, value))
    return luaL_error(state, "address outside owned snapshot");
  call.failed = failed;
  lua_pushinteger(state, value);
  return 1;
}
int write(lua_State *state) {
  auto &call = invocation(state);
  const bool failed = call.failed;
  call.failed = true;
  const auto address = luaL_checkinteger(state, 1),
             value = luaL_checkinteger(state, 2);
  if (address < 0 || address > 65535 || value < 0 || value > 255 ||
      !invocation(state).setByte(address, value))
    return luaL_error(state, "RAM mutation denied or invalid");
  call.failed = failed;
  return 0;
}
int report(lua_State *state) {
  auto &call = invocation(state);
  const bool failed = call.failed;
  call.failed = true;
  std::size_t size;
  const char *message = luaL_checklstring(state, 1, &size);
  bool ok = false;
  try {
    ok = invocation(state).output({message, size});
  } catch (...) {
    invocation(state).failed = true;
  }
  if (!ok)
    return luaL_error(state, "report permission/budget rejected");
  call.failed = failed;
  return 0;
}
int setup(lua_State *state) {
  const luaL_Reg libraries[] = {{"_G", luaopen_base},
                                {LUA_MATHLIBNAME, luaopen_math},
                                {LUA_STRLIBNAME, luaopen_string},
                                {LUA_TABLIBNAME, luaopen_table},
                                {LUA_UTF8LIBNAME, luaopen_utf8},
                                {nullptr, nullptr}};
  for (const auto *lib = libraries; lib->name; ++lib) {
    luaL_requiref(state, lib->name, lib->func, 1);
    lua_pop(state, 1);
  }
  // Scripts cannot load files or change the budget hook through these
  // libraries.
  for (const auto *name : {"dofile", "loadfile", "load"}) {
    lua_pushnil(state);
    lua_setglobal(state, name);
  }
  lua_getglobal(state, "string");
  // Lua pattern backtracking does not call the VM hook. Reject these APIs
  // rather than allowing unmetered native work inside a budgeted invocation.
  for (const auto *name : {"find", "match", "gmatch", "gsub"}) {
    lua_pushnil(state);
    lua_setfield(state, -2, name);
  }
  lua_pop(state, 1);
  const luaL_Reg bindings[] = {{"reg", reg},       {"setreg", setreg},
                               {"read8", read},    {"write8", write},
                               {"report", report}, {nullptr, nullptr}};
  luaL_newlib(state, bindings);
  lua_setglobal(state, "time");
  return 0;
}
} // namespace
Result evaluateLua(std::string_view source, Invocation &call) {
  Heap heap{0, call.limits.heapBytes};
  lua_State *state = lua_newstate(allocate, &heap);
  if (!state)
    return {.error = "Lua heap exhausted during initialization"};
  *static_cast<Invocation **>(lua_getextraspace(state)) = &call;
  lua_sethook(state, hook, LUA_MASKCOUNT, 1);
  lua_pushcfunction(state, setup);
  int status = lua_pcall(state, 0, 0, 0);
  if (status == LUA_OK)
    status = luaL_loadbufferx(state, source.data(), source.size(), "automation",
                              "t");
  if (status == LUA_OK)
    status = lua_pcall(state, 0, 0, 0);
  Result result;
  if (status == LUA_OK && !call.failed)
    result.success = true;
  else {
    const auto *error =
        lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : nullptr;
    result.error = error ? error : "Lua invocation rejected";
  }
  call.accepting = false;
  lua_close(state);
  if (call.failed) {
    result.success = false;
    if (result.error.empty())
      result.error = "Lua binding/finalizer invocation rejected";
  }
  return result;
}
} // namespace BMMQ::Script
#else
namespace BMMQ::Script {
Result evaluateLua(std::string_view, Invocation &) {
  return {.error = "Lua runtime unavailable"};
}
} // namespace BMMQ::Script
#endif
