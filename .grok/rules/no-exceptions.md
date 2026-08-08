# Hard rule: no C++ exceptions

Never add `try`, `catch`, or `throw` to DarkEngine or Sandbox code.

Never use `std::runtime_error`, `std::invalid_argument`, or other exception types for control flow.

Use:

- `bool` / status returns
- `DE_LOG_ERROR` / `DE_LOG_FATAL`
- `DE_ASSERT` for programmer errors
- HRESULT checks with log + return

`noexcept` on special members is fine.

If existing code throws and you are editing it, migrate that path off exceptions rather than expanding try/catch.
