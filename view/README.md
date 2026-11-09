# ATM+ SE Frontend (`view/`)

Security-engineered hardware kiosk simulator and zero-trust thin client for the **ATM+ SE** banking platform (`Even Pointers Are Smart`). Built with pure Vanilla HTML5, CSS3, and modern ES6+ JavaScript modules with zero external runtime or build dependencies.

---

## 1. Architectural Principles

### 1.1. Zero-Trust Thin Client
- **Strict Separation of Concerns**: The frontend acts purely as a hardware display and input peripheral (`CardSlot`, `PinPad`, `BillAcceptor`, `ReceiptPrinter`, `CRT/TFT Display`).
- **Backend Authority**: Business invariants, PIN verification, role resolution (`UserRole::Customer` vs `UserRole::Engineer`), greedy banknote dispensing (`CashVault`), overdraft coverage (`Checking` + `Savings`), overflow sweeps, and SHA-256 + HMAC audit chaining (`AuditLog`) are enforced exclusively by the backend (`AtmController`) or its faithful in-memory emulator in `js/api.js`.
- **Automatic Role Resolution**: Users never pick their role in the UI. Role transitions (`CustomerSession` vs `EngineerSession`) are driven solely by the backend response to `POST /api/auth/pin`.

### 1.2. Client-Side Security Controls
- **PIN Confidentiality**: PIN keystrokes are held in a private class field (`#pinBuffer` in `js/components/pin-pad.js`), masked on-screen as `*`, and wiped immediately upon submission, cancellation, or session timeout.
- **XSS Prevention**: All dynamic values rendered into templates pass through context-aware HTML entity escaping (`escapeHtml` in `js/utils/dom.js`) or `textContent`.
- **Inactivity Session Watchdog**: `InactivityTimer` (`js/utils/timer.js`) monitors user pointer and keyboard events and automatically invokes `POST /api/card/eject` after `45` seconds of inactivity.

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
    ├── config.js               # Environment settings (USE_MOCK, API_BASE_URL, timeouts)
    ├── state.js                # Reactive store and FSM state constants
    ├── api.js                  # Unified HTTP fetch client and stateful Mock backend adapter
    ├── mock-data.js            # Initial cards, vault cassettes, and SHA-256/HMAC audit chain
    ├── components/
    │   ├── card-slot.js        # Motorized card reader slot and interactive test wallet
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

## 3. Running the Frontend

Because the application uses native ES6 modules (`<script type="module">`), browsers enforce CORS policies that require serving files over `http://` or `https://` rather than opening `index.html` via the `file://` protocol.

### Option A: Python Built-in HTTP Server
Run from the repository root or `view/` directory:
```bash
python -m http.server 3000 --directory view
```
Then open `http://localhost:3000` in your browser.

### Option B: Node.js (`npx serve`)
```bash
npx serve view -l 3000
```

### Option C: VS Code Live Server / C++ `cpp-httplib` Static Mount
Mount the `view/` directory in `HttpServerAdapter`:
```cpp
_server.set_mount_point("/", "./view");
```
Then navigate to `http://localhost:8080`.

---

## 4. Mock Mode vs Live C++ Backend

The bottom status bar provides real-time environment controls:
- **`[x] Mock Mode`**: Routes all API calls through the in-memory stateful emulator inside `js/api.js` with simulated network latency (`160-280ms`). Supports full persistence within the browser session and a **Reset Mock State** button to restore initial balances, cassettes, PIN counters, and audit records.
- **`[ ] Live Backend`**: Sends standard JSON HTTP requests via `fetch` to `CONFIG.API_BASE_URL` (default `/api` when served by the C++ backend, or `http://localhost:8080/api` when hosted externally). You can edit the target URL directly in the status bar and click **Apply**.

---

## 5. Interactive Test Cards

The right-hand **Test Card Wallet** dock provides four pre-configured smart cards to test every FSM transition and edge case in a single click:

| Card Label | Card Number (PAN) | Holder Name | PIN | Master Key | Role | Initial Accounts / Behavior |
|---|---|---|---|---|---|---|
| **Customer Valid** | `4111-2222-3333-4444` | `IVAN PETRENKO` | `1234` | N/A | `Customer` | Checking: `$1,540.00` (`154000` cents, min `$100.00`, overflow cap `$5,000.00`)<br>Savings: `$4,500.00` (`450000` cents) |
| **Card Blocked** | `4111-0000-0000-9999` | `BLOCKED USER` | `9999` | N/A | `Customer` | Immediately rejected on insertion with `CardBlocked` (`403`) |
| **Zero Bal / Expired** | `4111-5555-6666-7777` | `OLENA KOVALENKO` | `4321` | N/A | `Customer` | Checking: `$0.00`, Savings: `$0.00` (test `InsufficientFunds`). Includes a toggle button on the card to test `CardExpired` rejection on insert |
| **Engineer Service** | `9999-8888-7777-0001` | `SERVICE ENGINEER #42` | `0000` | `ENG-MASTER-KEY-2026` | `Engineer` | Unlocks Engineer Maintenance UI (`Vault Diagnostics`, `Cash Replenishment`, `Audit Log & Integrity`) |

---

## 6. Backend API Contract Reference

All monetary amounts and banknote denominations are transmitted as **integer cents** (`int64_t`), matching `core/enums.hpp` (`D_10 = 1000`, `D_20 = 2000`, `D_50 = 5000`, `D_100 = 10000`, `D_200 = 20000`, `D_500 = 50000`).

### 6.1. Session & Authentication Endpoints

#### `POST /api/card/insert`
Validates card status and transitions FSM from `Idle` to `CardInserted`.
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
    "maskedPan": "4111-****-****-4444",
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
    "token": "sess_...",
    "role": "Customer",
    "cardHolder": "IVAN PETRENKO",
    "remainingAttempts": 3
  }
  ```
- **Error Responses**: `401` (`InvalidPin` with `remainingAttempts`), `403` (`MaxAttemptsExceeded` / `CardBlocked`).

#### `POST /api/card/eject`
Ends the active session and returns FSM to `Idle`.
- **Request**: `{}`
- **Response (`200 OK`)**: `{"status": "ok"}`

---

### 6.2. Customer Banking Endpoints
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
    "receipt": {
      "txId": "TX-5-1234",
      "timestamp": "2026-11-09T13:10:00Z",
      "type": "WITHDRAWAL",
      "card": "4111-****-****-4444",
      "account": "Checking",
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
    "receipt": { "...": "..." }
  }
  ```

#### `POST /api/customer/transfer`
- **Request**:
  ```json
  { "from": "Checking", "to": "Savings", "amount": 20000 }
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "status": "ok",
    "newBalance": 84000,
    "receipt": { "...": "..." }
  }
  ```

---

### 6.3. Engineer Maintenance Endpoints
All Engineer endpoints require headers:
- `Authorization: Bearer <token>`
- `X-Master-Key: ENG-MASTER-KEY-2026`

#### `GET /api/engineer/vault`
- **Response (`200 OK`)**:
  ```json
  {
    "totalCash": 12500000,
    "doorOpen": false,
    "hardwareCounters": { "dispenseCycles": 14, "depositCycles": 6, "jamEvents": 0 },
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
    "totalCash": 13000000
  }
  ```

#### `GET /api/engineer/audit`
- **Response (`200 OK`)**:
  ```json
  {
    "records": [
      {
        "id": 1,
        "timestamp": "2026-11-09T09:00:00Z",
        "event": "SystemBoot",
        "detail": "HSM & CashVault initialized",
        "prevHash": "0000...",
        "hash": "8f4a..."
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
    "checkedRecords": 4,
    "brokenRecordId": null,
    "detail": "SHA-256 + HMAC chain verified (4 records intact)."
  }
  ```
