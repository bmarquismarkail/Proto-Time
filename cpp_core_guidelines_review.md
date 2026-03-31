Source reviewed: https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines
Codebase reviewed: /home/brandon/src/repos/Proto-Time

P.1: Express ideas directly in code - Pass: The runtime, executor, and plugin abstractions in /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:18 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:46 express the fetch/decode/execute pipeline directly.
P.2: Write in ISO Standard C++ - Pass: The project builds as C++20 in /home/brandon/src/repos/Proto-Time/CMakeLists.txt:5 and uses standard library facilities throughout the reviewed sources.
P.4: Ideally, a program should be statically type safe - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:101 uses a C-style cast from `DataType*` to `char*`, and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:24 overlays bitfields and a full value in a union.
P.5: Prefer compile-time checking to run-time checking - Pass: The code uses concepts and `static_assert` in /home/brandon/src/repos/Proto-Time/inst_cycle/opcode.hpp:58 and /home/brandon/src/repos/Proto-Time/tests/smoke_machine_boot.cpp:58.
P.6: What cannot be checked at compile time should be checkable at run time - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:123 returns `&mem[0]` when an address lookup fails, so invalid addresses are not reported at run time.
P.7: Catch run-time errors early - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:86 leaves `index` uninitialized when no writable region matches and then writes through it.
P.8: Don’t leak any resources - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 allocate with `new` and no owning destructor releases those allocations.
P.11: Encapsulate messy constructs, rather than spreading through the code - Pass: Plugin ABI details are isolated in /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:13 instead of being repeated across the codebase.
P.12: Use supporting tools as appropriate - Pass: /home/brandon/src/repos/Proto-Time/CMakeLists.txt:8 enables `-Wall -Wextra`, and the repository has smoke tests that pass under `ctest --test-dir build-working`.

I.1: Make interfaces explicit - Pass: /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:12 and /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:18 define clear abstract interfaces for machine control and execution.
I.4: Make interfaces precisely and strongly typed - Fail: /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:23 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:43 use string register names instead of typed register identifiers.
I.5: State preconditions (if any) - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:54 and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:86 require valid non-null buffers and mapped addresses but do not state those preconditions in the interface.
I.6: Prefer Expects() for expressing preconditions - Fail: No `Expects()`-style contract checks appear in the reviewed interfaces, including /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:54 and /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:52.
I.10: Use exceptions to signal a failure to perform a required task - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:54 returns `0` for missing registers, and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:113 silently ignores an invalid ROM destination.
I.11: Never transfer ownership by a raw pointer (T*) or reference (T&) - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 store owned heap allocations behind raw pointers.
I.12: Declare a pointer that must not be null as not_null - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:54 and /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:8 accept raw pointers that are immediately dereferenced without non-null enforcement.
I.13: Do not pass an array as a single pointer - Fail: /home/brandon/src/repos/Proto-Time/memory/MemoryPool.hpp:12, /home/brandon/src/repos/Proto-Time/memory/IMemory.hpp:8, and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:54 model ranges as `DataType*` plus count instead of a view type.
I.22: Avoid complex initialization of global objects - Pass: The review found no non-local mutable globals; plugin metadata statics in /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:104 and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy_plugin_runtime.hpp:10 are simple local statics.
I.23: Keep the number of function arguments low - Pass: Most public interfaces stay at three arguments or fewer, such as /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:27 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:43.
I.24: Avoid adjacent parameters that can be invoked by the same arguments in either order with different meaning - Fail: /home/brandon/src/repos/Proto-Time/memory/IMemory.hpp:8 and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:109 use adjacent same-type address/count parameters that are easy to swap.
I.25: Prefer empty abstract classes as interfaces to class hierarchies - Pass: /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:12 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:46 use abstract base classes as interfaces.
I.30: Encapsulate rule violations - Fail: Raw ownership and pointer arithmetic are spread across /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37, /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:28, and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:86 instead of being isolated behind a safer abstraction.

F.1: “Package” meaningful operations as carefully named functions - Pass: The code packages operations such as `fetch`, `decode`, `execute`, `recordIfNeeded`, and `loadScript` into named units across /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:27 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginExecutor.hpp:37.
F.3: Keep functions short and simple - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_interpreter.cpp:528 and neighboring decode helpers rely on long switch-heavy functions that mix register lookup, scratch memory, and flag computation.
F.7: For general use, take T* or T& arguments rather than smart pointers - Pass: Public interfaces use references and raw pointers rather than passing smart pointers around, for example /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginExecutor.hpp:27 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:43.
F.9: Unused parameters should be unnamed - Fail: /home/brandon/src/repos/Proto-Time/emulator.cpp:25 names `argc` and `argv` even though the build emits `-Wunused-parameter` warnings for both.
F.15: Prefer simple and conventional ways of passing information - Pass: The executor and runtime APIs mostly use direct return values and references, such as /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:27 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:43.
F.16: For “in” parameters, pass cheaply-copied types by value and others by reference to const - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:9 takes `std::string` by value in `hasRegister`, forcing an unnecessary copy.
F.20: For “out” output values, prefer return values to output parameters - Fail: /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:70, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:65, and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginExecutor.hpp:30 return success plus an optional `std::string* error` output parameter.
F.23: Use a not_null<T> to indicate that “null” is not a valid value - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:8 and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:54 require valid pointers but encode that requirement only informally.
F.24: Use a span<T> or a span_p<T> to designate a half-open sequence - Fail: /home/brandon/src/repos/Proto-Time/memory/IMemory.hpp:8 and /home/brandon/src/repos/Proto-Time/memory/MemorySnapshot/templ/MemorySnapshot.impl.hpp:11 use pointer-plus-count sequence APIs.
F.25: Use a zstring or a not_null<zstring> to designate a C-style string - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:14 takes `const char* name` for register lookup without expressing null-termination or non-nullness in the type.
F.26: Use a unique_ptr<T> to transfer ownership where a pointer is needed - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 allocate owned objects with raw pointers instead of `std::unique_ptr`.
F.46: int is the return type for main() - Pass: /home/brandon/src/repos/Proto-Time/emulator.cpp:25 defines `int main(...)`.
F.50: Use a lambda when a function won’t do (to capture local variables, or to write a local function) - Pass: Opcode emission in /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:216 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:81 uses local lambdas appropriately.
F.55: Don’t use va_arg arguments - Pass: The review found no variadic C-style argument handling.

C.1: Organize related data into structures (structs or classes) - Pass: /home/brandon/src/repos/Proto-Time/machine/CPU.hpp:12, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:33, and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:20 group related state cleanly.
C.9: Minimize exposure of members - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:41 exposes `RegisterFile`’s backing vector by non-const reference through `operator()`.
C.11: Make concrete types regular - Fail: `RegisterFile` and `RegisterInfo` are copyable by default even though they own raw pointers in /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:50.
C.20: If you can avoid defining default operations, do - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:49 rely on default copy operations for types with raw ownership semantics.
C.30: Define a destructor if a class needs an explicit action at object destruction - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:49 need destruction logic to release heap objects but define none.
C.31: All resources acquired by a class must be released by the class’s destructor - Fail: The allocations created in /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 are never released by their owning classes.
C.32: If a class has a raw pointer (T*) or reference (T&), consider whether it might be owning - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:50 use raw pointer members with ambiguous ownership.
C.33: If a class has an owning pointer member, define a destructor - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:49 holds `info` as an owning raw pointer but has no destructor.
C.35: A base class destructor should be either public and virtual, or protected and non-virtual - Fail: /home/brandon/src/repos/Proto-Time/machine/CPU.hpp:22 and /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:3 declare polymorphic bases without such a destructor.
C.41: A constructor should create a fully initialized object - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:10 can leave `info` null when the requested register does not exist.
C.42: If a constructor cannot construct a valid object, throw an exception - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:10 silently constructs a partially initialized `RegisterInfo`.
C.43: Ensure that a copyable class has a default constructor, a copy constructor, and a copy assignment operator - Fail: The default copy behavior of /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37 duplicates raw pointers instead of establishing clear value semantics.
C.46: By default, declare single-argument constructors explicit - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:53 declares a converting constructor for `RegisterInfo` without `explicit`.
C.48: Prefer in-class initializers to member initializers in constructors for constant initializers - Pass: /home/brandon/src/repos/Proto-Time/machine/CPU.hpp:13, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:14, and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:26 use in-class initializers effectively.
C.49: Prefer initialization to assignment in constructors - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:32 initializes register storage and memory by assignment inside the constructor body rather than via member initialization.
C.67: A polymorphic class should suppress public copy/move - Fail: /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:12, /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:18, and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:46 leave public copy/move operations implicitly available on polymorphic interfaces.
C.90: Rely on constructors and assignment operators, not memset and memcpy - Pass: The review found no `memset` or `memcpy` use in the C++ source set.

Enum.1: Prefer enumerations over macros - Pass: Execution guarantees, plugin kinds, and memory access flags are modeled as enums in /home/brandon/src/repos/Proto-Time/machine/RuntimeContext.hpp:12, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:29, and /home/brandon/src/repos/Proto-Time/memory/MemoryStorage.hpp:10.
Enum.3: Prefer class enums over “plain” enums - Fail: /home/brandon/src/repos/Proto-Time/memory/MemoryStorage.hpp:10 still uses a plain `enum memAccess`.

R.1: Manage resources automatically using resource handles and RAII - Fail: Register ownership in /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:49 is managed manually with raw pointers.
R.2: In interfaces, use raw pointers to denote individual objects (only) - Fail: /home/brandon/src/repos/Proto-Time/memory/IMemory.hpp:8 and /home/brandon/src/repos/Proto-Time/memory/MemoryPool.hpp:12 use raw pointers to denote sequences.
R.3: A raw pointer (a T*) is non-owning - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:50 use raw pointers as owning members.
R.5: Prefer scoped objects, don’t heap-allocate unnecessarily - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 allocates each register on the heap although the register file already owns a container that could store value types or smart pointers.
R.10: Avoid malloc() and free() - Pass: The review found no `malloc` or `free` usage.
R.11: Avoid calling new and delete explicitly - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 call `new` explicitly.
R.12: Immediately give the result of an explicit resource allocation to a manager object - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 stores raw results from `new` directly into the data model without an owning handle.
R.20: Use unique_ptr or shared_ptr to represent ownership - Fail: The ownership sites in /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 do not use smart pointers.

ES.20: Always initialize an object - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:88 and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:109 declare `index` without initialization before conditional assignment.
ES.30: Don’t use macros for program text manipulation - Pass: The only macros found are include guards.
ES.31: Don’t use macros for constants or “functions” - Pass: Constants and behavior are expressed with enums, functions, and `constexpr` values instead of function-like macros.
ES.42: Keep use of pointers simple and straightforward - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:101 and /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_interpreter.cpp:81 rely on low-level pointer arithmetic and raw pointer aliasing.
ES.47: Use nullptr rather than 0 or NULL - Pass: Null pointers are written as `nullptr` throughout the reviewed code.
ES.48: Avoid casts - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:101 uses a C-style cast, and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:15 and /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:56 rely on `dynamic_cast` to recover type information at run time.
ES.49: If you must use a cast, use a named cast - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:101 uses `(char*)stream`.
ES.60: Avoid `new` and `delete` outside resource management functions - Fail: Explicit allocation appears in ordinary register helpers at /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3.
ES.65: Don’t dereference an invalid pointer - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:123 can return the start of memory for an unmapped address, and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:26 dereferences `info` without proving it was initialized.
ES.66: Don’t try to avoid negative values by using unsigned - Pass: The code uses fixed-width integer types intentionally rather than substituting unsigned as a blanket safety policy.

Per.10: Rely on the static type system - Fail: Stringly typed register lookup in /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:23 and run-time casts in /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:15 reduce the benefits of the static type system.
Per.14: Minimize the number of allocations and deallocations - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/RegisterFile.impl.hpp:31 allocates one heap object per register and /home/brandon/src/repos/Proto-Time/memory/templ/RegisterInfo.impl.hpp:3 allocates metadata wrappers separately.

E.1: Develop an error-handling strategy early in a design - Fail: The codebase mixes exceptions, boolean return values plus `std::string* error`, silent `nullptr`/`0` returns, and unchecked fallbacks across /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:142, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:70, and /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:54.
E.2: Throw an exception to signal that a function can’t perform its assigned task - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:54 and /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:74 suppress invalid-state failures instead of signaling them.
E.6: Use RAII to prevent leaks - Fail: The register ownership model in /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:37 does not use RAII handles.
E.12: Use noexcept when exiting a function because of a throw is impossible or unacceptable - Fail: Trivial observer functions such as /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy_plugin_runtime.hpp:9, /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginExecutor.hpp:69, and /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:17 are not declared `noexcept` even though they are intended as non-throwing accessors.

Con.2: By default, make member functions const - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:58, /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:17, and /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:52 are read-only queries that are not marked `const`.
Con.3: By default, pass pointers and references to consts - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:13 and /home/brandon/src/repos/Proto-Time/memory/IMemory.hpp:8 expose mutable pointers in APIs that are often used for read-only access.

T.10: Specify concepts for all template arguments - Fail: Core templates such as /home/brandon/src/repos/Proto-Time/common_microcode.hpp:7, /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:11, and /home/brandon/src/repos/Proto-Time/memory/MemorySnapshot/SnapshotStorage/SnapshotStorage.h:19 are unconstrained.
T.11: Whenever possible use standard concepts - Pass: /home/brandon/src/repos/Proto-Time/inst_cycle/opcode.hpp:63 uses the standard `std::convertible_to` concept in its `is_opcode` check.
T.41: Require only essential properties in a template’s concepts - Fail: Because most templates have no concepts at all, callers get delayed substitution errors instead of minimal, explicit requirements.

CPL.1: Prefer C++ to C - Pass: The code uses C++ classes, templates, standard containers, lambdas, and concepts rather than writing the project in C.

SF.4: Include header files before other declarations in a file - Pass: Reviewed translation units such as /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:1 and /home/brandon/src/repos/Proto-Time/emulator.cpp:13 include headers before declarations.
SF.6: Use using namespace directives for transition, for foundation libraries (such as std), or within a local scope (only) - Pass: The review found no `using namespace` directives in the project sources.
SF.7: Don’t write using namespace at global scope in a header file - Pass: No global-scope `using namespace` directive appears in headers.
SF.8: Use #include guards for all header files - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:1 has no include guard.
SF.20: Use namespaces to express logical structure - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:3 and /home/brandon/src/repos/Proto-Time/cores/gameboy/GameBoyMachine.hpp:14 place major types in the global namespace instead of a project namespace.
SF.22: Use an unnamed (anonymous) namespace for all internal/non-exported entities - Pass: /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:12 uses an anonymous namespace for a local helper.

SL.1: Use libraries wherever possible - Pass: The implementation relies on standard library containers, strings, tuples, optional, and function wrappers throughout the codebase.
SL.2: Prefer the standard library to other libraries - Pass: The reviewed code uses the standard library almost exclusively.
SL.con.1: Prefer using STL array or vector instead of a C array - Fail: /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_interpreter.cpp:530 uses `bool newflags[4]`.
SL.con.2: Prefer using STL vector by default unless you have a reason to use a different container - Pass: Dynamic storage in /home/brandon/src/repos/Proto-Time/inst_cycle/fetch/fetchBlock.hpp:12, /home/brandon/src/repos/Proto-Time/memory/MemoryStorage.hpp:32, and executor code defaults to `std::vector`.
SL.con.3: Avoid bounds errors - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:123 and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:115 write through pointers without validating that the target span is large enough.
SL.con.4: don’t use memset or memcpy for arguments that are not trivially-copyable - Pass: No `memset` or `memcpy` use was found.
SL.str.1: Use std::string to own character sequences - Pass: Owned error and metadata text is represented with `std::string`, for example in /home/brandon/src/repos/Proto-Time/inst_cycle/executor/PluginContract.hpp:37 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/BlockScript.hpp:19.
SL.str.2: Use std::string_view or gsl::span<char> to refer to character sequences - Pass: Lookup APIs such as /home/brandon/src/repos/Proto-Time/machine/Machine.hpp:23 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:43 use `std::string_view`.
SL.io.3: Prefer iostreams for I/O - Pass: File and console I/O in /home/brandon/src/repos/Proto-Time/emulator.cpp:21 and /home/brandon/src/repos/Proto-Time/inst_cycle/executor/BlockScript.hpp:28 uses iostreams.
SL.io.50: Avoid endl - Pass: The review found no `std::endl` usage.

A.2: Express potentially reusable parts as a library - Pass: Core emulator logic is built as the reusable `time-gameboy-interpreter-core` library in /home/brandon/src/repos/Proto-Time/CMakeLists.txt:9.
A.4: There should be no cycles among libraries - Pass: The build defines a single core library and executable/test dependents, with no library cycle in the reviewed CMake graph.

Pro.bounds: Bounds safety profile - Fail: /home/brandon/src/repos/Proto-Time/memory/templ/MemoryStorage.impl.hpp:86 and /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:115 contain unchecked pointer-based range operations.
Pro.lifetime: Lifetime safety profile - Fail: /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:50 depend on raw owning pointers with default copy behavior.

GSL.owner: Ownership pointers - Fail: The project encodes ownership with naked pointers in /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:39 and /home/brandon/src/repos/Proto-Time/memory/reg_base.hpp:50 rather than an `owner<T*>`-style annotation or a stronger type.
GSL.assert: Assertions - Fail: Preconditions are mostly implicit; the only assertions are ad hoc `assert` calls such as /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:16 instead of a consistent contract/assertion layer.

NL.1: Don’t say in comments what can be clearly stated in code - Fail: /home/brandon/src/repos/Proto-Time/emulator.cpp:1 and /home/brandon/src/repos/Proto-Time/machine/Bus.hpp:4 use banner and explanatory comments that restate obvious context without clarifying behavior.
NL.2: State intent in comments - Pass: The opcode comment at /home/brandon/src/repos/Proto-Time/inst_cycle/opcode.hpp:4 and the anonymous-namespace helper comment in /home/brandon/src/repos/Proto-Time/cores/gameboy/gameboy.cpp:217 are intent-oriented.
NL.3: Keep comments crisp - Fail: The banner in /home/brandon/src/repos/Proto-Time/emulator.cpp:1 is long and low-signal for the current code.
NL.4: Maintain a consistent indentation style - Fail: The review found mixed indentation and brace layout across /home/brandon/src/repos/Proto-Time/cores/gameboy/decode/gb_decoder.hpp:3, /home/brandon/src/repos/Proto-Time/memory/MemorySnapshot/templ/MemorySnapshot.impl.hpp:6, and newer headers such as /home/brandon/src/repos/Proto-Time/inst_cycle/executor/Executor.hpp:14.
NL.8: Use a consistent naming style - Fail: The code mixes `GetRegister`, `getLastFeedback`, `addMemBlock`, `loadProgram`, and `recordIfNeeded` naming styles across the same codebase.

Not Applicable

I.3: Avoid singletons - Not Applicable: The reviewed codebase does not define a singleton object or singleton access pattern.
I.7: State postconditions - Not Applicable: The project does not use a formal postcondition system, and no public interface establishes explicit postcondition syntax to review separately.
I.8: Prefer Ensures() for expressing postconditions - Not Applicable: No `Ensures()`-style facility is present in the project.
I.9: If an interface is a template, document its parameters using concepts - Not Applicable: The project uses internal templates but does not expose a documented public template API surface that is separately described for consumers.
I.26: If you want a cross-compiler ABI, use a C-style subset - Not Applicable: The repository is not currently shipping a cross-compiler binary ABI beyond an internal plugin sketch.
I.27: For stable library ABI, consider the Pimpl idiom - Not Applicable: The codebase is not structured as a stable installed library with ABI compatibility commitments.
F.18: For “will-move-from” parameters, pass by X&& and std::move the parameter - Not Applicable: The reviewed interfaces do not define a move-consuming parameter API.
F.19: For “forward” parameters, pass by TP&& and only std::forward the parameter - Not Applicable: The project does not expose a forwarding-reference API.
F.21: To return multiple “out” values, prefer returning a struct - Not Applicable: The multi-value cases already use structs such as `StepResult`, so there is no separate violation pattern to assess.
F.27: Use a shared_ptr<T> to share ownership - Not Applicable: No shared-ownership design appears in the reviewed codebase.
F.42: Return a T* to indicate a position (only) - Not Applicable: The code does not use pointer return values to indicate container positions or iterators.
F.43: Never (directly or indirectly) return a pointer or a reference to a local object - Not Applicable: The review found no function returning the address or reference of a local object.
F.45: Don’t return a T&& - Not Applicable: No `T&&` return type appears in the reviewed sources.
F.47: Return T& from assignment operators - Not Applicable: The reviewed code does not define custom assignment operators.
F.48: Don’t return std::move(local) - Not Applicable: The review found no `return std::move(local);`.
F.49: Don’t return const T - Not Applicable: The reviewed functions do not return `const T` by value.
F.51: Where there is a choice, prefer default arguments over overloading - Not Applicable: The project has little overload-based API design to evaluate under this rule.
F.52: Prefer capturing by reference in lambdas that will be used locally, including passed to algorithms - Not Applicable: The local lambdas reviewed either capture nothing or capture small values intentionally.
F.53: Avoid capturing by reference in lambdas that will be used non-locally, including returned, stored on the heap, or passed to another thread - Not Applicable: The review found no non-local reference-capturing lambdas.
F.54: When writing a lambda that captures this or any class data member, don’t use [=] default capture - Not Applicable: The review found no `[=]` captures involving `this`.
F.56: Avoid unnecessary condition nesting - Not Applicable: The larger functions are complex, but the dominant issue is mixed responsibilities rather than gratuitous condition nesting alone.
C.7: Don’t define a class or enum and declare a variable of its type in the same statement - Not Applicable: This pattern does not appear in the reviewed code.
C.8: Use class rather than struct if any member is non-public - Not Applicable: The reviewed types follow this basic rule.
C.12: Don’t make data members const or references in a copyable or movable type - Not Applicable: The relevant copyable/movable types do not rely on const or reference data members.
C.21: If you define or =delete any copy, move, or destructor function, define or =delete them all - Not Applicable: The main ownership problem is absent special members rather than partially declared ones.
C.36: A destructor must not fail - Not Applicable: The relevant classes define no custom destructors.
C.37: Make destructors noexcept - Not Applicable: The codebase has no user-defined destructor bodies to review here.
C.50: Use a factory function if you need “virtual behavior” during initialization - Not Applicable: The reviewed constructors do not depend on virtual dispatch.
C.60: Make copy assignment non-virtual, take the parameter by const&, and return by non-const& - Not Applicable: No custom copy assignment operator is defined.
C.63: Make move assignment non-virtual, take the parameter by &&, and return by non-const& - Not Applicable: No custom move assignment operator is defined.
C.66: Make move operations noexcept - Not Applicable: No custom move operations are defined.
C.82: Don’t call virtual functions in constructors and destructors - Not Applicable: The review found no constructor or destructor performing virtual dispatch.
Enum.6: Avoid unnamed enumerations - Not Applicable: The reviewed code does not use unnamed enums.
R.4: A raw reference (a T&) is non-owning - Not Applicable: The lifetime issues found are with raw pointers rather than reference ownership confusion.
R.13: Perform at most one explicit resource allocation in a single expression statement - Not Applicable: Each explicit allocation site performs one allocation at a time.
R.14: Avoid [] parameters, prefer span - Not Applicable: The code uses pointer-plus-count rather than array syntax in parameters; that issue is already captured under I.13 and F.24.
R.15: Always overload matched allocation/deallocation pairs - Not Applicable: The project does not define custom allocation operators.
R.21: Prefer unique_ptr over shared_ptr unless you need to share ownership - Not Applicable: The code does not use smart pointers at all, so the more direct ownership rule violations above are the controlling findings.
R.22: Use make_shared() to make shared_ptrs - Not Applicable: No `shared_ptr` construction is present.
R.23: Use make_unique() to make unique_ptrs - Not Applicable: No `unique_ptr` construction is present.
R.24: Use std::weak_ptr to break cycles of shared_ptrs - Not Applicable: The code does not use `shared_ptr`.
CP.1: Assume that your code will run as part of a multi-threaded program - Not Applicable: The reviewed code contains no threads, atomics, mutexes, futures, or async execution.
CP.2: Avoid data races - Not Applicable: No concurrent execution surface appears in the codebase.
CP.3: Minimize explicit sharing of writable data - Not Applicable: No multi-threaded shared-state design is present.
CP.4: Think in terms of tasks, rather than threads - Not Applicable: The project does not define threaded or task-based parallel work.
CP.8: Don’t try to use volatile for synchronization - Not Applicable: No synchronization or `volatile`-based threading code is present.
CP.9: Whenever feasible use tools to validate your concurrent code - Not Applicable: No concurrent code is present.
CP.44: Remember to name your lock_guards and unique_locks - Not Applicable: No lock objects are present.
CP.50: Define a mutex together with the data it guards. Use synchronized_value<T> where possible - Not Applicable: No mutex-protected shared state is present.
E.3: Use exceptions for error handling only - Not Applicable: The reviewed exceptions are all used for error signaling rather than regular control flow.
E.13: Never throw while being the direct owner of an object - Not Applicable: The reviewed throwing paths are not in custom resource-owning destructors or analogous owner-cleanup code.
Con.1: By default, make objects immutable - Not Applicable: Emulator state is intentionally mutable, so this rule is too broad to apply mechanically without a narrower abstraction-specific target.
T.1: Minimize template metaprogramming - Not Applicable: The project uses templates, but not template metaprogramming-heavy techniques that dominate the implementation.
T.2: Use templates to express algorithms that apply to many argument types - Not Applicable: Existing templates are infrastructure types rather than a broad algorithm library.
CPL.2: If you must use C, use the common subset of C and C++, and compile the C code as C++ - Not Applicable: The repository does not contain C translation units.
CPL.3: If you must use C for interfaces, use C++ in the calling code using such interfaces - Not Applicable: The only C-like ABI surface is the plugin descriptor sketch, and it is not used as a compiled C interface in this tree.
SF.1: Use a .cpp suffix for code files and .h for interface files if your project doesn’t already follow another convention - Not Applicable: The project already uses a consistent `.cpp` and `.hpp` convention.
SF.2: A header file must not contain object definitions or non-inline function definitions - Not Applicable: The remaining header definitions are templates or class-inline definitions, which are permitted for this convention.
SF.3: Use header files for all declarations used in multiple source files - Not Applicable: The reviewed shared declarations are already in headers.
SF.5: A .cpp file must include the header file(s) that defines its interface - Not Applicable: The reviewed `.cpp` files already include their corresponding interface header.
SF.9: Avoid cyclic dependencies among source files - Not Applicable: The small build graph reviewed does not show evidence of cyclic library dependencies.
SF.10: Avoid dependencies on implicitly #included names - Not Applicable: The reviewed files include the headers they directly rely on in the inspected surface area.
SF.11: Header files should be self-contained - Not Applicable: The major issue in header hygiene is the missing guard in `gb_decoder.hpp`; no separate self-containment failure was confirmed during the build.
SF.12: Prefer the quoted form of #include for files relative to the including file and the angle bracket form everywhere else - Not Applicable: The reviewed includes follow that convention.
SF.13: Use portable header identifiers in #include statements - Not Applicable: The include paths reviewed are portable relative paths.
SF.21: Don’t use an unnamed (anonymous) namespace in a header - Not Applicable: No header defines an anonymous namespace.
SL.3: Do not add non-standard entities to namespace std - Not Applicable: The code does not modify `namespace std`.
SL.4: Use the standard library in a type-safe manner - Not Applicable: The higher-signal issues are raw ownership and unchecked indexing rather than misuse of standard-library APIs themselves.
SL.str.3: Use zstring or czstring to refer to a C-style, zero-terminated, sequence of characters - Not Applicable: The only significant C-string use was already called out under F.25.
SL.str.4: Use char* to refer to a single character - Not Applicable: The code does not use `char*` to model single characters.
SL.str.5: Use std::byte to refer to byte values that do not necessarily represent characters - Not Applicable: The project consistently uses `uint8_t` for emulator byte values.
SL.str.10: Use std::string when you need to perform locale-sensitive string operations - Not Applicable: The project does not perform locale-sensitive string processing.
SL.str.11: Use gsl::span<char> rather than std::string_view when you need to mutate a string - Not Applicable: The reviewed APIs do not mutate strings through views.
SL.str.12: Use the s suffix for string literals meant to be standard-library strings - Not Applicable: The code does not rely on implicit `std::string` literal construction patterns where this rule would matter.
SL.io.1: Use character-level input only when you have to - Not Applicable: The codebase does not implement character-by-character input parsing outside standard stream tokenization.
SL.io.2: When reading, always consider ill-formed input - Not Applicable: The file/script readers do some validation, but the review did not isolate a separate ill-formed-input bug beyond the broader error-handling findings above.
SL.io.10: Unless you use printf-family functions call ios_base::sync_with_stdio(false) - Not Applicable: The program is not an iostream-heavy CLI where this performance rule is relevant.
SL.regex: Regex - Not Applicable: The codebase does not use regular expressions.
SL.chrono: Time - Not Applicable: The codebase does not use `<chrono>`.
SL.C.1: Don’t use setjmp/longjmp - Not Applicable: The project does not use `setjmp` or `longjmp`.
A.1: Separate stable code from less stable code - Not Applicable: The repository is small enough that this architectural partition is not separately expressed in the current layout.
NR.1: Don’t insist that all declarations should be at the top of a function - Not Applicable: The review found no local style policy conflict on this point.
NR.2: Don’t insist on having only a single return-statement in a function - Not Applicable: The code already uses early returns freely.
NR.3: Don’t avoid exceptions - Not Applicable: The codebase does use exceptions in several decoding paths.
NR.4: Don’t insist on placing each class definition in its own source file - Not Applicable: The project already uses a pragmatic file layout.
NR.5: Don’t use two-phase initialization - Not Applicable: The principal initialization issue is incomplete construction in `RegisterInfo`, already captured under C.41 and C.42.
NR.6: Don’t place all cleanup actions at the end of a function and goto exit - Not Applicable: The code does not use `goto`-based cleanup.
NR.7: Don’t make data members protected - Not Applicable: The review found no problematic use of protected data members.
RF.rules: Coding rules - Not Applicable: This is a reference section rather than a guideline to apply to the code itself.
RF.books: Books with coding guidelines - Not Applicable: This is a reference section rather than a code rule.
RF.web: Websites - Not Applicable: This is a reference section rather than a code rule.
RF.man: Manuals - Not Applicable: This is a reference section rather than a code rule.
RF.core: Core Guidelines materials - Not Applicable: This is a reference section rather than a code rule.
Pro.safety: Type-safety profile - Not Applicable: The more specific safety profile findings are already captured under `Pro.bounds` and `Pro.lifetime`.
GSL.view: Views - Not Applicable: The project does not use GSL view types, so the more direct finding is the absence of `span`-style APIs already recorded above.
GSL.util: Utilities - Not Applicable: The codebase does not use GSL utility facilities.
GSL.concept: Concepts - Not Applicable: The project uses standard concepts and custom concepts, but not the GSL concept helpers.
GSL.ptr: Smart pointer concepts - Not Applicable: The codebase does not use GSL pointer concept helpers.
NL.5: Avoid encoding type information in names - Not Applicable: The naming problems observed are inconsistency and mixed style rather than explicit Hungarian-style type encoding.
NL.7: Make the length of a name roughly proportional to the length of its scope - Not Applicable: Name length was not a significant maintainability issue in the reviewed files.
NL.9: Use ALL_CAPS for macro names only - Not Applicable: The reviewed ALL_CAPS names are include guards or enum-style constants.
NL.10: Prefer underscore_style names - Not Applicable: The codebase has mixed naming conventions rather than a single enforceable style baseline.
NL.11: Make literals readable - Not Applicable: Hex literals are domain-specific opcode and address constants; readability issues are secondary to the stronger correctness findings above.
NL.15: Use spaces sparingly - Not Applicable: Whitespace issues are already captured under indentation/style consistency.
NL.16: Use a conventional class member declaration order - Not Applicable: Member declaration order was not a primary issue in the reviewed types.
NL.17: Use K&R-derived layout - Not Applicable: The codebase does not follow one consistent brace style, but the broader finding is style inconsistency rather than a single alternative house style.
NL.18: Use C++-style declarator layout - Not Applicable: Declarator layout was not a primary correctness or maintainability problem in the reviewed files.
NL.19: Avoid names that are easily misread - Not Applicable: The naming issues observed are consistency and stringly typed identifiers rather than visually confusable names.
NL.20: Don’t place two statements on the same line - Not Applicable: This pattern was not a notable issue in the reviewed code.
NL.21: Declare one name (only) per declaration - Not Applicable: Multi-name declarations were not a significant issue in the reviewed files.
NL.25: Don’t use void as an argument type - Not Applicable: The codebase does not use `void` in argument lists.
NL.26: Use conventional const notation - Not Applicable: The reviewed code generally follows conventional `const` placement.
NL.27: Use a .cpp suffix for code files and .h for interface files - Not Applicable: The project intentionally uses `.hpp` for headers as its established convention.
