# ATM+ SE Frontend (`view/`)

Security-engineered hardware kiosk simulator and zero-trust thin client for the **ATM+ SE** banking platform (`Even Pointers Are Smart`). Built with pure Vanilla HTML5, CSS3, and modern ES6+ JavaScript modules with zero external runtime or build dependencies, connected directly to the C++23 `cpp-httplib` + SQLite3 + OpenSSL backend.

---

## 1. Architectural Principles

### 1.1. Zero-Trust Thin Client
- **Strict Separation of Concerns**: The frontend acts solely as a hardware display and input terminal (`CardSlot`, `PinPad`, `BillAcceptor`, `ReceiptPrinter`, `CRT/TFT Display`).
- **Backend Authority**: All business invariants, PIN verification (`AuthService`), role resolution (`UserRole::Customer` vs `UserRole::Engineer`), optimal banknote dispensing (`CashVault`), overdraft coverage (`Checking` + `Savings`), overflow sweeps, anti-replay protection (`AntiReplayManager`), and SHA-256 + HMAC audit chaining (`AuditLog`) are enforced exclusively by the C++ backend (`AtmController` / `HttpServerAdapter`).
- **Automatic Role Resolution**: Users never select a role in the UI. Session role transitions (`CustomerSession` vs `EngineerSession`) are driven solely by the backend response to `POST /api/auth/pin`.

### 1.2. Client-Side Security Controls
- **PIN Confidentiality**: PIN keystrokes are held in a private class field (`#pinBuffer` in `js/components/pin-pad.js`), masked on-screen as `*`, and wiped immediately upon submission, cancellation, or session timeout.
- **XSS Prevention**: All dynamic values rendered into DOM templates pass through context-aware HTML entity escaping (`escapeHtml` in `js/utils/dom.js`) or `textContent`.
- **Inactivity Session Watchdog**: `InactivityTimer` (`js/utils/timer.js`) monitors pointer and keyboard activity and invokes `POST /api/card/eject` after `45` seconds of inactivity (matching the backend `60s` session timeout).

---

## 2. Directory Structure

```text
view/
├── index.html                  # Single-page application shell and hardware layout
├── README.md                   # Technical documentation and API contract reference
├── assets/
│   └── chip.svg                # EMV smart card contact pad vector graphic
├── css/
│   ├── variables.css           # Design tokens, color system, typography, motion rules
│   ├── layout.css              # ATM chassis, bezel function keys (F1-F8), responsive grid
│   ├── atm-screen.css          # Recessed CRT/TFT screen states, forms, softkey slots
│   ├── components.css          # Card reader slot, PIN pad, cash shutter, receipt, cards
│   └── engineer.css            # Maintenance console, cassette meters, audit log table
└── js/
    ├── app.js                  # Application controller wiring state, timer, and views
    ├── config.js               # Environment settings (API_BASE_URL, timeout, demo cards)
    ├── state.js                # Reactive store and FSM state constants
    ├── api.js                  # Pure HTTP fetch client for the C++ backend API
    ├── components/
    │   ├── card-slot.js        # Motorized card reader slot, demo wallet, and custom PAN input
    │   ├── pin-pad.js          # Encrypted PIN pad (mouse + physical keyboard support)
    │   ├── customer-ui.js      # Customer flows: inquiry, withdraw, deposit, transfer
    │   ├── engineer-ui.js      # Engineer flows: master key, vault, replenish, audit verify
    │   ├── bill-acceptor.js    # Motorized shutter for banknote deposits and dispensing
    │   └── receipt.js          # Thermal 80mm transaction receipt printer
    └── utils/
        ├── dom.js              # HTML escaping, currency formatting, PAN masking
        └── timer.js            # Inactivity countdown timer
```

---

## 3. Building and Running with the C++ Backend

The C++ backend (`AtmPlusSe`) initializes and seeds `./atm.db`, mounts `view/` automatically at `/`, and exposes all `/api/*` endpoints on `http://localhost:8080`.

### Option A: Run Directly via the C++ Backend (Recommended)
1. Build the project with CMake (after Conan dependency setup):
   ```bash
   cmake --build out/build --config Release
   ```
2. Start the backend executable from the repository root (or from `out/build/Release/`):
   ```bash
   ./out/build/Release/AtmPlusSe.exe
   ```
3. Open `http://localhost:8080` in your browser.
   When served from port `8080` (or same origin), `CONFIG.API_BASE_URL` automatically resolves to `/api`.

### Option B: Separate Static Dev Server
If serving `view/` separately (for example on port `3000` or VS Code Live Server):
```bash
python -m http.server 3000 --directory view
```
- Start `AtmPlusSe` on port `8080`.
- When running from another port, `CONFIG.API_BASE_URL` defaults to `http://localhost:8080/api`.
- If the static server origin differs from `http://localhost:8080`, set `ATM_ALLOWED_ORIGIN` on the backend process (e.g. `ATM_ALLOWED_ORIGIN=http://localhost:3000`) so CORS headers match.
- You can also override the target API URL at runtime using the **API URL** input in the bottom status bar and clicking **Apply**.

### Environment Variables Supported by Backend
- `ATM_HMAC_SECRET`: Overrides the default HMAC secret used by `AuditLog` and database seeding.
- `ATM_ENGINEER_MASTER_KEY`: Overrides the default Engineer Master Key (`ENG-MASTER-KEY-2026`).
- `ATM_ALLOWED_ORIGIN`: Sets `Access-Control-Allow-Origin` (defaults to `http://localhost:8080`).

---

## 4. Seeded Database Cards & Accounts

On first startup, `storage::seed_demo_data` (`modules/storage/seed.cpp`) seeds SQLite (`./atm.db`) with four test cards, linked Checking/Savings accounts, and six vault cassettes (`$10` to `$500`, total `$125,000.00`). These match the **Test Card Wallet** dock in the UI:

| Card Label | Card Number (PAN) | Holder Name | PIN | Master Key | Role | Seeded Accounts / Behavior |
|---|---|---|---|---|---|---|
| **Customer Valid** | `4111-2222-3333-4444` | `IVAN PETRENKO` | `1234` | N/A | `Customer` | Checking `CHK-4444`: `$1,540.00` (`154000` cents, min `$100.00`, overflow cap `$5,000.00`)<br>Savings `SAV-4444`: `$4,500.00` (`450000` cents) |
| **Card Blocked** | `4111-0000-0000-9999` | `BLOCKED USER` | `9999` | N/A | `Customer` | `is_blocked = true`; rejected on insert with `CardBlocked` (`403`). Accounts: `CHK-9999` (`$820.00`), `SAV-9999` (`$1,200.00`) |
| **Zero Balance** | `4111-5555-6666-7777` | `OLENA KOVALENKO` | `4321` | N/A | `Customer` | Checking `CHK-7777`: `$0.00`, Savings `SAV-7777`: `$0.00` (tests `InsufficientFunds` and receiving external transfers) |
| **Engineer Service** | `9999-8888-7777-0001` | `SERVICE ENGINEER #42` | `0000` | `ENG-MASTER-KEY-2026` | `Engineer` | Unlocks Engineer Maintenance UI (`Vault Diagnostics`, `Cash Replenishment`, `Audit Log & Integrity`) |

The wallet dock also includes a **Custom Card PAN** form to submit arbitrary card numbers (e.g. to verify `CardNotFound` / `404` handling).

---

## 5. Backend API Contract Reference

All monetary amounts and banknote denominations are transmitted as **integer cents** (`int64_t`), matching `core/enums.hpp` (`D_10 = 1000`, `D_20 = 2000`, `D_50 = 5000`, `D_100 = 10000`, `D_200 = 20000`, `D_500 = 50000`).

### 5.1. Session & Authentication Endpoints

#### `POST /api/card/insert`
Validates card status and transitions the FSM from `Idle` to `CardInserted`.
- **Request**:
  ```json
  { "cardNumber": "4111-2222-3333-4444" }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "requiresPin": true,
    "cardHolder": "IVAN PETRENKO",
    "maskedPan": "****-****-****-4444",
    "remainingAttempts": 3
  }
  ```
- **Error Responses**: `403` (`CardBlocked`, `CardExpired`), `404` (`CardNotFound`).

#### `POST /api/auth/pin`
Verifies PIN against `AuthService`. Decrements `remainingAttempts` on failure and blocks the card when `remainingAttempts` reaches `0`.
- **Request**:
  ```json
  { "cardNumber": "4111-2222-3333-4444", "pin": "1234" }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "token": "<session-uuid>",
    "role": "Customer",
    "cardHolder": "IVAN PETRENKO",
    "remainingAttempts": 3
  }
  ```
- **Error Responses**: `401` (`InvalidPin` with `remainingAttempts`), `403` (`MaxAttemptsExceeded` / `CardBlocked` with `cardBlocked: true`).

#### `POST /api/card/eject`
Ends the active session and returns FSM to `Idle`.
- **Request**: `{}`
- **Response (`200 OK`)**: `{"status": "ok"}`

---

### 5.2. Customer Banking Endpoints
All Customer endpoints require header `Authorization: Bearer <token>`.

#### `GET /api/customer/accounts`
- **Response (`200 OK`)**:
  ```json
  {
    "accounts": [
      { "id": "CHK-4444", "type": "Checking", "balance": 154000, "clientMin": 10000, "overflowMax": 500000 },
      { "id": "SAV-4444", "type": "Savings", "balance": 450000 }
    ]
  }
  ```

#### `POST /api/customer/withdraw`
- **Request**:
  ```json
  { "accountType": "Checking", "amount": 50000 }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "dispensedBills": [
      { "denomination": 20000, "count": 2 },
      { "denomination": 10000, "count": 1 }
    ],
    "newBalance": 104000,
    "overdraftCoveredCents": 0,
    "accounts": [ "..." ],
    "receipt": {
      "txId": "TX-5A1B2C3D",
      "timestamp": "2026-11-15T13:10:00Z",
      "type": "WITHDRAWAL",
      "card": "****-****-****-4444",
      "account": "CHK-4444",
      "amountCents": 50000,
      "newBalanceCents": 104000,
      "authHash": "A1B2C3D4E5F60718"
    }
  }
  ```

#### `POST /api/customer/deposit`
- **Request**:
  ```json
  { "accountType": "Checking", "bills": [10000, 20000, 20000] }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "totalDeposited": 50000,
    "overflowSweptCents": 0,
    "newBalance": 204000,
    "accounts": [ "..." ],
    "receipt": { "..." }
  }
  ```

#### `POST /api/customer/transfer`
Supports transfers to own linked account (`Checking`/`Savings`), external account IDs (`CHK-7777`, `SAV-7777`), or external card PANs (`4111-5555-6666-7777`), with per-request `idempotencyKey`.
- **Request**:
  ```json
  {
    "from": "Checking",
    "to": "Savings",
    "amount": 20000,
    "idempotencyKey": "<uuid>"
  }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "newBalance": 84000,
    "accounts": [ "..." ],
    "receipt": { "..." }
  }
  ```

---

### 5.3. Engineer Maintenance Endpoints
All Engineer endpoints require headers:
- `Authorization: Bearer <token>`
- `X-Master-Key: <engineer-master-key>` (default `ENG-MASTER-KEY-2026`)

#### `GET /api/engineer/vault`
- **Response (`200 OK`)**:
  ```json
  {
    "totalCash": 12500000,
    "doorOpen": false,
    "hardwareCounters": { "dispenseCycles": 0, "depositCycles": 0, "jamEvents": 0 },
    "cassettes": [
      { "denom": 1000, "count": 100, "capacity": 2000 },
      { "denom": 2000, "count": 200, "capacity": 2000 },
      { "denom": 5000, "count": 200, "capacity": 2000 },
      { "denom": 10000, "count": 300, "capacity": 2000 },
      { "denom": 20000, "count": 150, "capacity": 2000 },
      { "denom": 50000, "count": 100, "capacity": 2000 }
    ]
  }
  ```

#### `POST /api/engineer/vault-door`
- **Request**: `{"open": true}`
- **Response (`200 OK`)**: `{"status": "ok", "doorOpen": true}`

#### `POST /api/engineer/replenish`
Requires safe door to be open (`doorOpen: true`).
- **Request**:
  ```json
  { "cassetteIndex": 0, "addCount": 500 }
  ```
  *(Or `{"resetCounters": true}` to zero electromechanical cycle counters.)*
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "cassette": { "denom": 1000, "count": 600, "capacity": 2000 },
    "totalCash": 13000000,
    "doorOpen": true,
    "hardwareCounters": { "dispenseCycles": 0, "depositCycles": 0, "jamEvents": 0 },
    "cassettes": [ "..." ]
  }
  ```

#### `GET /api/engineer/audit`
Supports optional pagination parameters `?offset=<n>&limit=<n>`.
- **Response (`200 OK`)**:
  ```json
  {
    "records": [
      {
        "id": 1,
        "timestamp": "2026-11-15T13:00:00Z",
        "event": "REPLENISHMENT",
        "detail": "ATM_VAULT->BANK_TREASURY",
        "amountCents": 100000,
        "prevHash": "0000...",
        "hash": "8f4a...",
        "tampered": false
      }
    ]
  }
  ```

#### `POST /api/engineer/verify-integrity`
Verifies the SHA-256 + HMAC audit log hash chain (`AuditLog::verify_integrity_chain`).
- **Request**: `{}`
- **Response (`200 OK`)**:
  ```json
  {
    "valid": true,
    "checkedRecords": 12,
    "brokenRecordId": null,
    "detail": "SHA-256 + HMAC chain verified (12 records intact)."
  }
  ```
