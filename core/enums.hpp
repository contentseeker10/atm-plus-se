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

enum class ServiceType
{
    Withdrawal,
    Deposit,
    Transfer,
    BalanceInquiry,
    EngineerMaintenance
};

enum class UserRole
{
    Customer,
    Engineer,
    None
};

enum class Denomination
{
    D_10 = 1000,
    D_20 = 2000,
    D_50 = 5000,
    D_100 = 10000,
    D_200 = 20000,
    D_500 = 50000
};

enum class AtmError
{   
    // ATM specific
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
    VaultDoorOpen,

    // Auth specific
    CardNotFound,
    CardBlocked,
    InvalidPin,
    MaxAttemptsExceeded,
    CardExpired,
    CryptoVerificationFailed,
    DatabaseError,
    
    // DB specific
    DatabaseError
};

enum class AuthError
{
    CardNotFound,
    CardBlocked,
    InvalidPin,
    MaxAttemptsExceeded,
    CardExpired,
    CryptoVerificationFailed,

    DatabaseError,
};

constexpr AtmError to_atm_error(AuthError err) noexcept
{
    switch (err)
    {
    case AuthError::CardNotFound:             return AtmError::CardNotFound;
    case AuthError::CardBlocked:              return AtmError::CardBlocked;
    case AuthError::InvalidPin:               return AtmError::InvalidPin;
    case AuthError::MaxAttemptsExceeded:      return AtmError::MaxAttemptsExceeded;
    case AuthError::CardExpired:              return AtmError::CardExpired;
    case AuthError::CryptoVerificationFailed: return AtmError::CryptoVerificationFailed;
    case AuthError::DatabaseError:            return AtmError::DatabaseError;
    default:                                  return AtmError::Unauthorized;
    }
}

enum class DbError
{
    ConnectionFailed,
    QueryFailed,
    RecordNotFound,
    ConstraintViolation,
    TransactionFailed,
    Busy
};

constexpr AuthError to_auth_error(DbError err) noexcept {
    switch (err)
    {
        case DbError::ConnectionFailed:    return AuthError::DatabaseError;
        case DbError::QueryFailed:         return AuthError::DatabaseError;
        case DbError::RecordNotFound:      return AuthError::CardNotFound;
        case DbError::ConstraintViolation: return AuthError::DatabaseError;
        case DbError::TransactionFailed:   return AuthError::DatabaseError;
        case DbError::Busy:                return AuthError::DatabaseError;
        default:                           return AuthError::DatabaseError;
    }
}

enum class CryptoError
{
    HashMismatch,
    HmacVerificationFailed,
    DatabaseError,
    EntryNotBalanced,
    InvalidPreviousHash,
    InternalError
};