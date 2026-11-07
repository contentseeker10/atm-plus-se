/*
 *
 * ATM Simulator with Security Engineering
 * Developed by Stepanov V., Handziur D., Zolotarov O.
 *
 * Autumn 2026
 *
 * File: enums.hpp
 * Enumerations for access control module.
 *
 */

#pragma once

enum class ServiceType {
    Withdrawal,
    Deposit,
    Transfer,
    BalanceInquiry,
    EngineerMaintenance
};

enum class UserRole {
    Customer,
    Engineer,
    None
};

enum class Denomination {
    D_10 = 1000,
    D_20 = 2000,
    D_50 = 5000,
    D_100 = 10000,
    D_200 = 20000,
    D_500 = 50000
};

enum class AtmError {
    InvalidStateTransition,
    NoActiveSession,
    SessionExpired,
    InvalidSessionToken,
    ReplayAttackDetected,
    Unauthorized,
    VaultAccessDenied,
    HardwareFailure,
    InternalError,
    InsufficientCashInAtm,
    InvalidAmount,
    CassetteFull,
    VaultDoorOpen
};

enum class AuthError {
    CardNotFound,
    CardBlocked,
    InvalidPin,
    MaxAttemptsExceeded,
    CardExpired,
    CryptoVerificationFailed,
    DatabaseError
};

enum class DbError {
    ConnectionFailed,
    QueryFailed,
    RecordNotFound,
    ConstraintViolation,
    TransactionFailed,
    Busy
};