# tests/include

单核工程，无跨核命令协议，故本目录当前没有协议头。

目录保留是因为 `cpu1_crypto_api_test/CMakeLists.txt` 的
`target_include_directories` 指向它（与 `common_sources.cmake` 的约定一致）。
若后续加双核报文，协议头放这里。
