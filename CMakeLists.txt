cmake_minimum_required(VERSION 3.20)

project(AtmPlusSe LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(httplib REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(OpenSSL REQUIRED)

add_executable(AtmPlusSe 
    main/main.cpp

    core/http_server.cpp
    core/db.cpp
    core/crypto.cpp

    modules/access_control/atm_controller.cpp
    modules/access_control/atm_state.cpp
    modules/access_control/auth_service.cpp
    modules/access_control/session_context.cpp
    
    modules/cash/cash_vault.cpp
    modules/cash/cassette.cpp
    
    modules/accounts/account.cpp
    modules/accounts/checking_account.cpp
    modules/accounts/savings_account.cpp
    
    modules/accounting/ledger.cpp
    modules/accounting/audit.cpp
    
    modules/security/anti_replay.cpp

    modules/storage/operations.cpp
    modules/storage/row_mapping.cpp
    modules/storage/sqlite_utils.cpp
    modules/storage/transaction.cpp

    modules/accounting/ledger_service.cpp
)

target_include_directories(AtmPlusSe PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/modules
)

target_link_libraries(AtmPlusSe PRIVATE httplib::httplib)
target_link_libraries(AtmPlusSe PRIVATE nlohmann_json::nlohmann_json)
target_link_libraries(AtmPlusSe PRIVATE SQLite::SQLite3)
target_link_libraries(AtmPlusSe PRIVATE openssl::openssl)

if (MSVC)
    add_compile_options(/Wall /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wshadow -Wnon-virtual-dtor -pedantic)
endif()

add_executable(atm_tests
    tests/test_main.cpp
    tests/test_spec_tcs.cpp
    tests/test_aps.cpp
    tests/test_ledger_persistence.cpp

    modules/accounting/ledger.cpp
    modules/accounting/audit.cpp
    modules/accounting/ledger_service.cpp

    modules/accounts/account.cpp
    modules/accounts/checking_account.cpp
    modules/accounts/savings_account.cpp

    modules/security/anti_replay.cpp

    modules/storage/operations.cpp
    modules/storage/row_mapping.cpp
    modules/storage/sqlite_utils.cpp
    modules/storage/transaction.cpp

    core/crypto.cpp
    core/db.cpp
)

target_include_directories(atm_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_SOURCE_DIR}/modules
)

target_link_libraries(atm_tests PRIVATE SQLite::SQLite3)
target_link_libraries(atm_tests PRIVATE openssl::openssl)

if (MSVC)
    target_compile_options(atm_tests PRIVATE /utf-8)
else()
    target_compile_options(atm_tests PRIVATE -Wall -Wextra -Wshadow -pedantic)
endif()

enable_testing()
add_test(NAME accounting COMMAND atm_tests)
