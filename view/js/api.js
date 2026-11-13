import { CONFIG } from './config.js';
import {
  INITIAL_TEST_CARDS,
  INITIAL_VAULT_STATE,
  buildInitialAuditLog,
  computeRecordHash
} from './mock-data.js';
import { maskCardNumber } from './utils/dom.js';

export class ApiError extends Error {
  constructor(message, status = 400, errorCode = 'InternalError', payload = null) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.errorCode = errorCode;
    this.payload = payload;
  }
}

let mockDb = createFreshMockDb();

function createFreshMockDb() {
  return {
    cards: structuredClone(INITIAL_TEST_CARDS),
    vault: structuredClone(INITIAL_VAULT_STATE),
    audit: buildInitialAuditLog(),
    activeSession: null
  };
}

export function resetMockState() {
  mockDb = createFreshMockDb();
}

export function getTestCardsSnapshot() {
  return mockDb.cards.map((card) => ({
    id: card.id,
    label: card.label,
    cardNumber: card.cardNumber,
    cardHolder: card.cardHolder,
    pin: card.pin,
    masterKey: card.masterKey || null,
    role: card.role,
    blocked: card.blocked,
    expired: card.expired,
    remainingAttempts: card.remainingAttempts,
    theme: card.theme,
    badge: card.badge
  }));
}

export function toggleTestCardExpired(cardNumber) {
  const card = mockDb.cards.find((c) => c.cardNumber === cardNumber);
  if (!card) return null;
  card.expired = !card.expired;
  return card.expired;
}

export function toggleMockAuditTamper() {
  if (mockDb.audit.length < 2) return false;
  const target = mockDb.audit[1];
  if (!target.tampered) {
    target._originalAmount = target.amountCents;
    target.amountCents += 900000;
    target.detail = `${target.detail} [MODIFIED]`;
    target.tampered = true;
    return true;
  }
  target.amountCents = target._originalAmount ?? 50000;
  target.detail = target.detail.replace(' [MODIFIED]', '');
  target.tampered = false;
  return false;
}

function simulateDelay() {
  const span = CONFIG.MOCK_LATENCY_MAX_MS - CONFIG.MOCK_LATENCY_MIN_MS;
  const ms = CONFIG.MOCK_LATENCY_MIN_MS + Math.floor(Math.random() * (span + 1));
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function nowIso() {
  return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

function appendAudit(event, detail, amountCents = 0) {
  const last = mockDb.audit[mockDb.audit.length - 1];
  const prevHash = last
    ? last.hash
    : '0000000000000000000000000000000000000000000000000000000000000000';
  const timestamp = nowIso();
  const hash = computeRecordHash(prevHash, timestamp, event, detail, amountCents);
  const record = {
    id: mockDb.audit.length + 1,
    timestamp,
    event,
    detail,
    amountCents,
    prevHash,
    hash,
    tampered: false
  };
  mockDb.audit.push(record);
  return record;
}

function calculateTotalVaultCash() {
  return mockDb.vault.cassettes.reduce((sum, c) => sum + c.denom * c.count, 0);
}

function calculateGreedyDispense(amountCents) {
  let remaining = amountCents;
  const plan = [];
  const sorted = [...mockDb.vault.cassettes]
    .map((c, idx) => ({ ...c, index: idx }))
    .sort((a, b) => b.denom - a.denom);

  for (const cassette of sorted) {
    if (remaining <= 0) break;
    const needed = Math.floor(remaining / cassette.denom);
    const take = Math.min(needed, cassette.count);
    if (take > 0) {
      plan.push({ denomination: cassette.denom, count: take, index: cassette.index });
      remaining -= take * cassette.denom;
    }
  }

  if (remaining !== 0) {
    return null;
  }
  return plan;
}

function requireCustomerSession(headers) {
  const auth = headers?.Authorization || '';
  const token = auth.replace(/^Bearer\s+/i, '');
  if (!mockDb.activeSession || mockDb.activeSession.token !== token) {
    throw new ApiError('Session expired or invalid token.', 401, 'InvalidSessionToken');
  }
  if (mockDb.activeSession.role !== 'Customer') {
    throw new ApiError('Customer role required for this operation.', 403, 'Unauthorized');
  }
  const card = mockDb.cards.find((c) => c.cardNumber === mockDb.activeSession.cardNumber);
  if (!card) {
    throw new ApiError('Card record not found.', 404, 'CardNotFound');
  }
  return card;
}

function requireEngineerSession(headers) {
  const auth = headers?.Authorization || '';
  const token = auth.replace(/^Bearer\s+/i, '');
  const masterKey = headers?.['X-Master-Key'] || '';

  if (!mockDb.activeSession || mockDb.activeSession.token !== token) {
    throw new ApiError('Engineer session token is invalid.', 401, 'InvalidSessionToken');
  }
  if (mockDb.activeSession.role !== 'Engineer') {
    throw new ApiError('Engineer role required.', 403, 'Unauthorized');
  }
  if (masterKey !== mockDb.vault.masterKey) {
    appendAudit('SecurityAlert', 'Invalid Engineer Master Key attempt', 0);
    throw new ApiError('Invalid Engineer Master Key. Vault access denied.', 403, 'VaultAccessDenied');
  }
}

async function handleMockRequest(method, path, body = null, headers = {}) {
  await simulateDelay();

  if (method === 'POST' && path === '/card/insert') {
    const cleanPan = String(body?.cardNumber || '').trim();
    const card = mockDb.cards.find((c) => c.cardNumber === cleanPan);
    if (!card) {
      throw new ApiError('Unrecognized card number.', 404, 'CardNotFound');
    }
    if (card.blocked) {
      appendAudit('CardRejected', `Blocked card ${maskCardNumber(cleanPan)}`, 0);
      throw new ApiError('This card is permanently blocked by the issuer.', 403, 'CardBlocked');
    }
    if (card.expired) {
      appendAudit('CardRejected', `Expired card ${maskCardNumber(cleanPan)}`, 0);
      throw new ApiError('This card has expired and cannot be used.', 403, 'CardExpired');
    }

    mockDb.activeSession = {
      cardNumber: card.cardNumber,
      token: null,
      role: 'None'
    };

    return {
      status: 'ok',
      requiresPin: true,
      cardHolder: card.cardHolder,
      maskedPan: maskCardNumber(card.cardNumber),
      remainingAttempts: card.remainingAttempts
    };
  }

  if (method === 'POST' && path === '/auth/pin') {
    const cleanPan = String(body?.cardNumber || '').trim();
    const pin = String(body?.pin || '').trim();
    const card = mockDb.cards.find((c) => c.cardNumber === cleanPan);

    if (!card) {
      throw new ApiError('Card not found.', 404, 'CardNotFound');
    }
    if (card.blocked) {
      throw new ApiError('Card is blocked due to security policy.', 403, 'CardBlocked');
    }

    if (card.pin !== pin) {
      card.remainingAttempts = Math.max(0, card.remainingAttempts - 1);
      appendAudit('AuthFailed', `Invalid PIN on ${maskCardNumber(cleanPan)}`, 0);

      if (card.remainingAttempts === 0) {
        card.blocked = true;
        mockDb.activeSession = null;
        appendAudit('CardBlocked', `Max PIN attempts exceeded for ${maskCardNumber(cleanPan)}`, 0);
        throw new ApiError(
          'Maximum PIN attempts exceeded. Card has been blocked.',
          403,
          'MaxAttemptsExceeded',
          { remainingAttempts: 0, cardBlocked: true }
        );
      }

      throw new ApiError(
        `Incorrect PIN code. Remaining attempts: ${card.remainingAttempts}.`,
        401,
        'InvalidPin',
        { remainingAttempts: card.remainingAttempts }
      );
    }

    card.remainingAttempts = 3;
    const token = `sess_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 8)}`;
    mockDb.activeSession = {
      cardNumber: card.cardNumber,
      token,
      role: card.role
    };

    appendAudit('SessionAuth', `${card.role} authenticated (${maskCardNumber(cleanPan)})`, 0);

    return {
      status: 'ok',
      token,
      role: card.role,
      cardHolder: card.cardHolder,
      remainingAttempts: 3
    };
  }

  if (method === 'POST' && path === '/card/eject') {
    if (mockDb.activeSession?.cardNumber) {
      appendAudit('SessionEnd', `Card ejected (${maskCardNumber(mockDb.activeSession.cardNumber)})`, 0);
    }
    mockDb.activeSession = null;
    return { status: 'ok' };
  }

  if (method === 'GET' && path === '/customer/accounts') {
    const card = requireCustomerSession(headers);
    return {
      accounts: structuredClone(card.accounts)
    };
  }

  if (method === 'POST' && path === '/customer/withdraw') {
    const card = requireCustomerSession(headers);
    const accountType = body?.accountType || 'Checking';
    const amount = Number(body?.amount);

    if (mockDb.vault.doorOpen) {
      throw new ApiError('Vault door is open. Customer operations are locked.', 503, 'VaultDoorOpen');
    }
    if (!Number.isInteger(amount) || amount <= 0 || amount % 1000 !== 0) {
      throw new ApiError('Amount must be a positive multiple of $10.00.', 400, 'InvalidAmount');
    }

    const primaryAcc = card.accounts.find((a) => a.type === accountType);
    const savingsAcc = card.accounts.find((a) => a.type === 'Savings');
    if (!primaryAcc) {
      throw new ApiError('Selected account not found.', 404, 'RecordNotFound');
    }

    let overdraftCoveredCents = 0;
    if (accountType === 'Checking') {
      const clientMin = primaryAcc.clientMin ?? 0;
      const projected = primaryAcc.balance - amount;
      if (projected < clientMin) {
        const deficit = clientMin - projected;
        if (!savingsAcc || savingsAcc.balance < deficit) {
          throw new ApiError(
            'Insufficient funds (including linked Savings overdraft coverage).',
            400,
            'InsufficientFunds'
          );
        }
        overdraftCoveredCents = deficit;
      }
    } else if (primaryAcc.balance < amount) {
      throw new ApiError('Insufficient funds in Savings account.', 400, 'InsufficientFunds');
    }

    const dispensePlan = calculateGreedyDispense(amount);
    if (!dispensePlan) {
      throw new ApiError(
        'ATM cannot dispense the exact requested amount with available cassettes.',
        409,
        'InsufficientCashInAtm'
      );
    }

    for (const item of dispensePlan) {
      mockDb.vault.cassettes[item.index].count -= item.count;
    }
    mockDb.vault.hardwareCounters.dispenseCycles += 1;

    if (overdraftCoveredCents > 0 && savingsAcc) {
      savingsAcc.balance -= overdraftCoveredCents;
      primaryAcc.balance += overdraftCoveredCents;
      appendAudit(
        'OverdraftCover',
        `${savingsAcc.id}->${primaryAcc.id}`,
        overdraftCoveredCents
      );
    }

    primaryAcc.balance -= amount;
    const record = appendAudit('Withdrawal', `${primaryAcc.id}->ATM_VAULT`, amount);

    const dispensedBills = dispensePlan.map(({ denomination, count }) => ({
      denomination,
      count
    }));

    return {
      status: 'ok',
      dispensedBills,
      newBalance: primaryAcc.balance,
      overdraftCoveredCents,
      accounts: structuredClone(card.accounts),
      receipt: {
        txId: `TX-${record.id}-${Date.now().toString().slice(-4)}`,
        timestamp: record.timestamp,
        type: 'WITHDRAWAL',
        card: maskCardNumber(card.cardNumber),
        account: accountType,
        amountCents: amount,
        overdraftCoveredCents,
        newBalanceCents: primaryAcc.balance,
        authHash: record.hash.slice(0, 16).toUpperCase()
      }
    };
  }

  if (method === 'POST' && path === '/customer/deposit') {
    const card = requireCustomerSession(headers);
    const accountType = body?.accountType || 'Checking';
    const bills = Array.isArray(body?.bills) ? body.bills.map(Number) : [];

    if (mockDb.vault.doorOpen) {
      throw new ApiError('Vault door is open. Customer operations are locked.', 503, 'VaultDoorOpen');
    }
    if (bills.length === 0) {
      throw new ApiError('Please insert at least one banknote to deposit.', 400, 'InvalidAmount');
    }

    const countsByDenom = new Map();
    for (const denom of bills) {
      if (!CONFIG.DENOMINATIONS_CENTS.includes(denom)) {
        throw new ApiError(`Unsupported banknote denomination: ${denom}.`, 400, 'InvalidAmount');
      }
      countsByDenom.set(denom, (countsByDenom.get(denom) || 0) + 1);
    }

    for (const [denom, addCount] of countsByDenom.entries()) {
      const cassette = mockDb.vault.cassettes.find((c) => c.denom === denom);
      if (!cassette || cassette.count + addCount > cassette.capacity) {
        throw new ApiError(
          `Cassette for $${denom / 100} is full and cannot accept more notes.`,
          409,
          'CassetteFull'
        );
      }
    }

    for (const [denom, addCount] of countsByDenom.entries()) {
      const cassette = mockDb.vault.cassettes.find((c) => c.denom === denom);
      cassette.count += addCount;
    }
    mockDb.vault.hardwareCounters.depositCycles += 1;

    const totalDeposited = bills.reduce((sum, b) => sum + b, 0);
    const primaryAcc = card.accounts.find((a) => a.type === accountType);
    const savingsAcc = card.accounts.find((a) => a.type === 'Savings');

    primaryAcc.balance += totalDeposited;
    const record = appendAudit('Deposit', `ATM_VAULT->${primaryAcc.id}`, totalDeposited);

    let overflowSweptCents = 0;
    if (
      accountType === 'Checking' &&
      primaryAcc.overflowMax &&
      primaryAcc.balance > primaryAcc.overflowMax &&
      savingsAcc
    ) {
      overflowSweptCents = primaryAcc.balance - primaryAcc.overflowMax;
      primaryAcc.balance -= overflowSweptCents;
      savingsAcc.balance += overflowSweptCents;
      appendAudit('OverflowSweep', `${primaryAcc.id}->${savingsAcc.id}`, overflowSweptCents);
    }

    return {
      status: 'ok',
      totalDeposited,
      overflowSweptCents,
      newBalance: primaryAcc.balance,
      accounts: structuredClone(card.accounts),
      receipt: {
        txId: `TX-${record.id}-${Date.now().toString().slice(-4)}`,
        timestamp: record.timestamp,
        type: 'DEPOSIT',
        card: maskCardNumber(card.cardNumber),
        account: accountType,
        amountCents: totalDeposited,
        overflowSweptCents,
        newBalanceCents: primaryAcc.balance,
        authHash: record.hash.slice(0, 16).toUpperCase()
      }
    };
  }

  if (method === 'POST' && path === '/customer/transfer') {
    const card = requireCustomerSession(headers);
    const fromType = body?.from || 'Checking';
    const toTarget = String(body?.to || 'Savings').trim();
    const amount = Number(body?.amount);

    if (!Number.isInteger(amount) || amount <= 0) {
      throw new ApiError('Transfer amount must be greater than $0.00.', 400, 'InvalidAmount');
    }

    const fromAcc = card.accounts.find((a) => a.type === fromType);
    if (!fromAcc) {
      throw new ApiError('Source account not found.', 404, 'RecordNotFound');
    }
    if (fromType === toTarget) {
      throw new ApiError('Source and destination accounts must be different.', 400, 'InvalidAmount');
    }

    const minAllowed = fromAcc.clientMin ?? 0;
    if (fromAcc.balance - amount < minAllowed) {
      throw new ApiError(
        `Insufficient funds (Checking minimum reserve is $${(minAllowed / 100).toFixed(2)}).`,
        400,
        'InsufficientFunds'
      );
    }

    const ownToAcc = card.accounts.find((a) => a.type === toTarget);
    let targetLabel = toTarget;
    if (ownToAcc) {
      fromAcc.balance -= amount;
      ownToAcc.balance += amount;
      targetLabel = ownToAcc.id;
    } else {
      fromAcc.balance -= amount;
      targetLabel = maskCardNumber(toTarget);
    }

    const record = appendAudit('Transfer', `${fromAcc.id}->${targetLabel}`, amount);

    return {
      status: 'ok',
      newBalance: fromAcc.balance,
      accounts: structuredClone(card.accounts),
      receipt: {
        txId: `TX-${record.id}-${Date.now().toString().slice(-4)}`,
        timestamp: record.timestamp,
        type: `TRANSFER TO ${toTarget.toUpperCase()}`,
        card: maskCardNumber(card.cardNumber),
        account: fromType,
        amountCents: amount,
        newBalanceCents: fromAcc.balance,
        authHash: record.hash.slice(0, 16).toUpperCase()
      }
    };
  }

  if (method === 'GET' && path === '/engineer/vault') {
    requireEngineerSession(headers);
    return {
      totalCash: calculateTotalVaultCash(),
      doorOpen: mockDb.vault.doorOpen,
      hardwareCounters: structuredClone(mockDb.vault.hardwareCounters),
      cassettes: structuredClone(mockDb.vault.cassettes)
    };
  }

  if (method === 'POST' && path === '/engineer/vault-door') {
    requireEngineerSession(headers);
    const nextOpen = typeof body?.open === 'boolean' ? body.open : !mockDb.vault.doorOpen;
    mockDb.vault.doorOpen = nextOpen;
    appendAudit('VaultDoor', nextOpen ? 'Safe door OPENED by Engineer' : 'Safe door LOCKED by Engineer', 0);
    return {
      status: 'ok',
      doorOpen: mockDb.vault.doorOpen
    };
  }

  if (method === 'POST' && path === '/engineer/replenish') {
    requireEngineerSession(headers);
    if (!mockDb.vault.doorOpen) {
      throw new ApiError(
        'Safe door is locked. Open the vault door before servicing cassettes.',
        409,
        'VaultAccessDenied'
      );
    }

    if (body?.resetCounters) {
      mockDb.vault.hardwareCounters = { dispenseCycles: 0, depositCycles: 0, jamEvents: 0 };
      appendAudit('CountersReset', 'Engineer reset electromechanical counters', 0);
      return {
        status: 'ok',
        totalCash: calculateTotalVaultCash(),
        doorOpen: mockDb.vault.doorOpen,
        hardwareCounters: structuredClone(mockDb.vault.hardwareCounters),
        cassettes: structuredClone(mockDb.vault.cassettes)
      };
    }

    const idx = Number(body?.cassetteIndex);
    const addCount = Number(body?.addCount);
    const cassette = mockDb.vault.cassettes[idx];

    if (!cassette) {
      throw new ApiError('Invalid cassette index.', 400, 'InvalidAmount');
    }
    if (!Number.isInteger(addCount) || addCount <= 0) {
      throw new ApiError('Replenish bill count must be a positive integer.', 400, 'InvalidAmount');
    }
    if (cassette.count + addCount > cassette.capacity) {
      throw new ApiError(
        `Cassette capacity (${cassette.capacity} notes) exceeded.`,
        409,
        'CassetteFull'
      );
    }

    cassette.count += addCount;
    appendAudit(
      'VaultReplenish',
      `Added ${addCount}x$${cassette.denom / 100} to Cassette #${idx + 1}`,
      addCount * cassette.denom
    );

    return {
      status: 'ok',
      cassette: structuredClone(cassette),
      totalCash: calculateTotalVaultCash(),
      cassettes: structuredClone(mockDb.vault.cassettes)
    };
  }

  if (method === 'GET' && path === '/engineer/audit') {
    requireEngineerSession(headers);
    return {
      records: structuredClone(mockDb.audit).reverse()
    };
  }

  if (method === 'POST' && path === '/engineer/verify-integrity') {
    requireEngineerSession(headers);
    let expectedPrev = '0000000000000000000000000000000000000000000000000000000000000000';
    for (const rec of mockDb.audit) {
      const recomputed = computeRecordHash(
        expectedPrev,
        rec.timestamp,
        rec.event,
        rec.detail,
        rec.amountCents
      );
      if (rec.prevHash !== expectedPrev || rec.hash !== recomputed || rec.tampered) {
        return {
          valid: false,
          checkedRecords: mockDb.audit.length,
          brokenRecordId: rec.id,
          detail: `Hash mismatch detected at Record #${rec.id} (${rec.event}).`
        };
      }
      expectedPrev = rec.hash;
    }

    return {
      valid: true,
      checkedRecords: mockDb.audit.length,
      brokenRecordId: null,
      detail: `SHA-256 + HMAC chain verified (${mockDb.audit.length} records intact).`
    };
  }

  throw new ApiError(`Unknown endpoint: ${method} ${path}`, 404, 'InternalError');
}

async function request(method, path, body = null, customHeaders = {}) {
  if (CONFIG.USE_MOCK) {
    return handleMockRequest(method, path, body, customHeaders);
  }

  const controller = new AbortController();
  const timeoutId = window.setTimeout(() => controller.abort(), 8000);

  try {
    const headers = {
      'Content-Type': 'application/json',
      ...customHeaders
    };
    const response = await fetch(`${CONFIG.API_BASE_URL}${path}`, {
      method,
      headers,
      body: body ? JSON.stringify(body) : undefined,
      signal: controller.signal
    });

    const text = await response.text();
    let payload = null;
    if (text) {
      try {
        payload = JSON.parse(text);
      } catch (_) {
        payload = { message: text };
      }
    }

    if (!response.ok) {
      throw new ApiError(
        payload?.message || `HTTP Error ${response.status}`,
        response.status,
        payload?.errorCode || 'InternalError',
        payload
      );
    }

    return payload;
  } catch (err) {
    if (err instanceof ApiError) throw err;
    if (err.name === 'AbortError') {
      throw new ApiError('Backend request timed out.', 504, 'DatabaseError');
    }
    throw new ApiError(
      `Cannot reach live backend at ${CONFIG.API_BASE_URL}. Switch to Mock Mode or start the C++ server.`,
      503,
      'ConnectionFailed'
    );
  } finally {
    window.clearTimeout(timeoutId);
  }
}

export const api = {
  insertCard(cardNumber) {
    return request('POST', '/card/insert', { cardNumber });
  },
  verifyPin(cardNumber, pin) {
    return request('POST', '/auth/pin', { cardNumber, pin });
  },
  ejectCard() {
    return request('POST', '/card/eject', {});
  },
  getCustomerAccounts(token) {
    return request('GET', '/customer/accounts', null, {
      Authorization: `Bearer ${token}`
    });
  },
  withdraw(token, accountType, amountCents) {
    return request(
      'POST',
      '/customer/withdraw',
      { accountType, amount: amountCents },
      { Authorization: `Bearer ${token}` }
    );
  },
  deposit(token, accountType, bills) {
    return request(
      'POST',
      '/customer/deposit',
      { accountType, bills },
      { Authorization: `Bearer ${token}` }
    );
  },
  transfer(token, from, to, amountCents) {
    return request(
      'POST',
      '/customer/transfer',
      { from, to, amount: amountCents },
      { Authorization: `Bearer ${token}` }
    );
  },
  getVaultDiagnostics(token, masterKey) {
    return request('GET', '/engineer/vault', null, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  setVaultDoor(token, masterKey, open) {
    return request(
      'POST',
      '/engineer/vault-door',
      { open },
      {
        Authorization: `Bearer ${token}`,
        'X-Master-Key': masterKey
      }
    );
  },
  replenishCassette(token, masterKey, payload) {
    return request('POST', '/engineer/replenish', payload, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  getAuditLog(token, masterKey) {
    return request('GET', '/engineer/audit', null, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  verifyIntegrity(token, masterKey) {
    return request(
      'POST',
      '/engineer/verify-integrity',
      {},
      {
        Authorization: `Bearer ${token}`,
        'X-Master-Key': masterKey
      }
    );
  }
};
