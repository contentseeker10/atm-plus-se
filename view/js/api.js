import { CONFIG } from './config.js';

export class ApiError extends Error {
  constructor(message, status = 400, errorCode = 'InternalError', payload = null) {
    super(message);
    this.name = 'ApiError';
    this.status = status;
    this.errorCode = errorCode;
    this.payload = payload;
  }
}

function generateIdempotencyKey() {
  if (typeof crypto !== 'undefined' && typeof crypto.randomUUID === 'function') {
    return crypto.randomUUID();
  }
  return `tx-${Date.now()}-${Math.random().toString(36).slice(2, 10)}`;
}

async function request(method, path, body = null, customHeaders = {}) {
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
      `Cannot reach backend at ${CONFIG.API_BASE_URL}. Ensure the C++ server is running.`,
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
      { from, to, amount: amountCents, idempotencyKey: generateIdempotencyKey() },
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
  getAuditLog(token, masterKey, { offset = 0, limit = 0 } = {}) {
    let path = '/engineer/audit';
    if (offset > 0 || limit > 0) {
      const params = new URLSearchParams({
        offset: String(offset),
        limit: String(limit)
      });
      path = `${path}?${params.toString()}`;
    }
    return request('GET', path, null, {
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
