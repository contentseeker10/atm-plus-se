# ATM Simulator with Security Engineering

Developed as a course project for "Methods of OOP" (Autumn 2026) by Stepanov V., Handziur D., Zolotarov O.

## Overview

A local ATM simulator with a C++23 HTTP backend, SQLite database, and a web-based hardware UI.

- **Customer mode:** card insertion, PIN login, balance check, cash withdrawal, cash deposit, and account transfers.
- **Engineer mode:** master key login, cash vault status, cassette replenishment, and audit log verification.
- **Security:** HMAC-chained audit log, anti-replay tokens, PIN attempt limits, and session timeouts.

## Structure

```text
atm-plus-se/
├── core/               # HTTP server, database manager, crypto helpers, enums
├── main/
│   └── main.cpp        # Program entry point
├── modules/            # Domain logic (access control, accounts, accounting, cash, security, storage)
├── tests/              # Unit and integration tests (atm_tests)
├── view/               # Web UI (HTML/CSS/JS) and frontend docs
├── CMakeLists.txt      # CMake configuration
└── conanfile.txt       # Conan dependencies
```

## Stack

**Backend:**
- C++23
- cpp-httplib
- nlohmann/json

**Database:**
- SQLite3

**Security:**
- OpenSSL

**Frontend:**
- Pure HTML/CSS/JS

**Build:**
- CMake (>= 3.20)
- Conan 2

## Build and Run

1. Install dependencies with Conan:
   ```bash
   conan install . --output-folder=out --build=missing -s build_type=Release -s compiler.cppstd=23
   ```

2. Configure and build with CMake:
   ```bash
   cmake -S . -B out/build -DCMAKE_TOOLCHAIN_FILE=out/build/generators/conan_toolchain.cmake -DCMAKE_POLICY_DEFAULT_CMP0091=NEW
   cmake --build out/build --config Release
   ```
   Optional: pass `-DATM_ENABLE_ASAN=ON` to CMake to enable AddressSanitizer.

3. Run the server from the repository root:
   ```bash
   ./out/build/Release/AtmPlusSe.exe
   ```
   On first run, the server creates and seeds `./atm.db` and serves the web UI at `http://localhost:8080`.

4. Run the tests:
   ```bash
   ctest --test-dir out/build -C Release --output-on-failure
   ```

## Environment Variables

- `ATM_HMAC_SECRET` — overrides the default HMAC secret for the audit log and database seeding.
- `ATM_ENGINEER_MASTER_KEY` — overrides the default engineer master key (`ENG-MASTER-KEY-2026`).
- `ATM_ALLOWED_ORIGIN` — sets `Access-Control-Allow-Origin` (default: `http://localhost:8080`).

For frontend details, API endpoints, and seeded test cards, see [view/README.md](view/README.md).
