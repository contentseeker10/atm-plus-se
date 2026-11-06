# ATM Simulator with Security Engineering

Developed as a course project for "Methods of OOP" (Autumn 2026) by Stepanov V., Handziur D., Zolotarov O.

## Structure

```
atm-plus-se/
├── core/               # Core backend handlers (HTTP server, DB manager)
├── main/
|   └── main.cpp        # Program's entry point 
├── modules/            # Project's inner modules (access control, cash vault, etc.)
├── CMakeLists.txt      # CMake configuration
└── conanfile.txt       # Conan configuration
```

## Stack

**Backend:**
- C++23
- cpp-httplib
- nlohmann/json

**Database:**
- SQLite3

**Frontend:**
- Pure HTML/CSS/JS