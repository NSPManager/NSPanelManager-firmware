#include "arch_rules.hpp"

#include <cstdlib>
#include <cstring>

#include <cxxabi.h>
#include <dlfcn.h>

namespace arch {
namespace {

struct Frame {
  const void *loop;
  const char *loop_name;
  esp_event_base_t base;
  int32_t id;
  const void *handler;
};

thread_local std::vector<Frame> g_stack;
std::vector<Violation> g_violations;

// Abort on the first violation instead of collecting them, so a debugger stops
// with the offending call still on the stack: ARCH_RULES=abort ./build/test_foo
bool abort_on_violation() {
  static const bool yes = [] {
    const char *v = std::getenv("ARCH_RULES");
    return v != nullptr && std::strcmp(v, "abort") == 0;
  }();
  return yes;
}

// Best-effort symbol name for a handler function pointer. Needs -rdynamic.
// Every firmware handler is a static class member, which has external linkage
// and resolves; an internal-linkage free function does not, so fall back to the
// address, which `addr2line -e <test binary>` will still place.
std::string handler_name(const void *fn) {
  Dl_info info;
  if (fn == nullptr) {
    return std::string();
  }
  if (dladdr(fn, &info) == 0 || info.dli_sname == nullptr) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%p", fn);
    return std::string(buf);
  }
  int status = 0;
  char *demangled = abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status);
  std::string out = (status == 0 && demangled != nullptr) ? demangled : info.dli_sname;
  std::free(demangled);
  // Handlers all share the esp_event_handler_t signature; the argument list adds
  // nothing but noise.
  const std::size_t paren = out.find('(');
  if (paren != std::string::npos) {
    out.erase(paren);
  }
  return out;
}

std::string describe(const Frame &f) {
  std::string out = "handler ";
  const std::string named = handler_name(f.handler);
  out += named.empty() ? "?" : named;
  out += " dispatching ";
  out += (f.base != nullptr ? f.base : "?");
  out += ":" + std::to_string(f.id);
  out += " on loop '";
  out += (f.loop_name != nullptr ? f.loop_name : "sys_evt");
  out += "'";
  return out;
}

void report(Rule rule, std::string call, std::string context) {
  g_violations.push_back({rule, std::move(call), std::move(context)});
  if (abort_on_violation()) {
    print();
    std::abort();
  }
}

std::string post_call(esp_event_base_t base, int32_t id, TickType_t ticks) {
  std::string out = "esp_event_post(";
  out += (base != nullptr ? base : "?");
  out += ", " + std::to_string(id) + ", ..., " + std::to_string(ticks) + " ticks)";
  return out;
}

} // namespace

const char *name(Rule rule) {
  switch (rule) {
  case Rule::BlockingInDispatch:
    return "blocking call inside a handler";
  case Rule::BlockingPostInDispatch:
    return "blocking post to another loop inside a handler";
  case Rule::BlockingPost:
    return "blocking post onto the shared default loop";
  }
  return "?";
}

const std::vector<Violation> &violations() { return g_violations; }

void reset() { g_violations.clear(); }

void print(std::FILE *out) {
  if (g_violations.empty()) {
    return;
  }
  std::fprintf(out, "\n%zu architecture rule violation(s):\n", g_violations.size());
  for (const Violation &v : g_violations) {
    std::fprintf(out, "  %s\n    %s\n", name(v.rule), v.call.c_str());
    if (!v.context.empty()) {
      std::fprintf(out, "    in %s\n", v.context.c_str());
    }
  }
}

int expect_clean() {
  print();
  return static_cast<int>(g_violations.size());
}

void enter_dispatch(const void *loop, const char *loop_name, esp_event_base_t base, int32_t id, const void *handler) {
  g_stack.push_back({loop, loop_name, base, id, handler});
}

void exit_dispatch() {
  if (!g_stack.empty()) {
    g_stack.pop_back();
  }
}

bool in_dispatch() { return !g_stack.empty(); }

const void *current_dispatch_loop() { return g_stack.empty() ? nullptr : g_stack.back().loop; }

void check_blocking_call(const char *call, TickType_t ticks) {
  if (ticks == 0 || g_stack.empty()) {
    return;
  }
  report(Rule::BlockingInDispatch, call, describe(g_stack.back()));
}

void check_post(const void *target_loop, esp_event_base_t base, int32_t id, TickType_t ticks) {
  if (ticks == 0) {
    return;
  }
  if (!g_stack.empty()) {
    // ESP-IDF clamps a post to the loop you are dispatching on down to a timeout
    // of 0, so only a cross-loop post can actually block here.
    if (target_loop != g_stack.back().loop) {
      report(Rule::BlockingPostInDispatch, post_call(base, id, ticks), describe(g_stack.back()));
    }
    return;
  }
  // Outside dispatch only the shared default queue is dangerous: a dedicated
  // loop's queue is its own and is not what Wi-Fi posts to.
  if (target_loop == nullptr) {
    report(Rule::BlockingPost, post_call(base, id, ticks), "an ordinary task");
  }
}

} // namespace arch
