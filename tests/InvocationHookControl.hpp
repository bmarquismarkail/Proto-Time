#pragma once
#include "inst_cycle/research/WholeBlock.hpp"
#include "machine/Machine.hpp"

// The memory compiler barrier keeps even the inlined lower-bound loop observable.
// These empty bodies are expressly not architectural execution or eligibility.
struct EmptyHookState {
  std::uint64_t calls = 0;
  void touch() {
    ++calls;
#if defined(__GNUC__) || defined(__clang__)
    asm volatile("" : "+m"(calls) : : "memory");
#endif
  }
  static bool before(void* opaque, const BMMQ::IR::Block&, std::size_t) {
    static_cast<EmptyHookState*>(opaque)->touch(); return true;
  }
  static bool retire(void* opaque, const BMMQ::IR::GuestInstruction&,
      const BMMQ::IR::InterpreterResult&, const BMMQ::IR::Research::Progress&) {
    static_cast<EmptyHookState*>(opaque)->touch(); return true;
  }
};
BMMQ::IR::Research::Hooks emptyHooks(EmptyHookState&);
std::uint64_t lookupPolicyBackend(const BMMQ::Machine&);
