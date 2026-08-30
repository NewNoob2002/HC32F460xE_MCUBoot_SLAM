# Configuration ownership

| Directory | Owner | Contents |
| --- | --- | --- |
| `Application/` | Firmware application | Middleware and application compile-time policy |
| `Memory/` | Firmware layout | Templates generated from canonical values in `cmake/MemoryMap.cmake` |
| `Product/` | Product definition | Hardware/board compatibility identity and public USB identity |

HC32 DDL selection, board pins and controller-specific settings belong to
`Platform/HC32F460/Config/`, not this project-level directory.
