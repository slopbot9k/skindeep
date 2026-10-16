/* Every suite, once. A new test file adds its name here. */
#define SK_SUITES(X)                                                         \
    X(arena)                                                                 \
    X(str)                                                                   \
    X(utf8)                                                                  \
    X(map)                                                                   \
    X(json)                                                                  \
    X(assets)                                                                \
    X(sha256)                                                                \
    X(base64)                                                                \
    X(http)                                                                  \
    X(paths)                                                                 \
    X(loop)                                                                  \
    X(relay)                                                                  \
    X(bus)                                                                    \
    X(tail)                                                                   \
    X(carry)                                                                  \
    X(agent)                                                                  \
    X(procfds)                                                                \
    X(codex_parse)                                                            \
    X(codex_thread_name)                                                      \
    X(codex_carry)                                                            \
    X(codex_discovery)                                                       \
    X(claude_carry)                                                           \
    X(claude_parse)                                                           \
    X(claude_core)                                                            \
    X(claude_settings)                                                        \
    X(claude_adapter)
