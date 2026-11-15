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
  withdraw(token, accountType, amountCents, nonce) {
    return request(
      'POST',
      '/customer/withdraw',
      {
        accountType,
        amount: amountCents,
        idempotencyKey: generateIdempotencyKey(),
        nonce
      },
      { Authorization: `Bearer ${token}` }
    );
  },
  deposit(token, accountType, bills, nonce) {
    return request(
      'POST',
      '/customer/deposit',
      {
        accountType,
        bills,
        idempotencyKey: generateIdempotencyKey(),
        nonce
      },
      { Authorization: `Bearer ${token}` }
    );
  },
  transfer(token, from, to, amountCents, nonce) {
    return request(
      'POST',
      '/customer/transfer',
      {
        from,
        to,
        amount: amountCents,
        idempotencyKey: generateIdempotencyKey(),
        nonce
      },
      { Authorization: `Bearer ${token}` }
    );
  },
  getAutoPayments(token) {
    return request('GET', '/customer/autopay', null, {
      Authorization: `Bearer ${token}`
    });
  },
  createAutoPayment(token, payload) {
    return request('POST', '/customer/autopay', payload, {
      Authorization: `Bearer ${token}`
    });
  },
  runAutoPayments(token) {
    return request('POST', '/customer/autopay/run', {}, {
      Authorization: `Bearer ${token}`
    });
  },
  deleteAutoPayment(token, ruleId) {
    return request('POST', '/customer/autopay/delete', { ruleId }, {
      Authorization: `Bearer ${token}`
    });
  },
  getVaultDiagnostics(token, masterKey) {
    return request('GET', '/engineer/vault', null, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  setVaultDoor(token, masterKey, open, forced = false) {
    return request(
      'POST',
      '/engineer/vault-door',
      { open, forced },
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
  getAuditLog(token, masterKey, { offset = 0, limit = 0, filter = {} } = {}) {
    const params = new URLSearchParams();
    if (offset > 0) params.set('offset', String(offset));
    if (limit > 0) params.set('limit', String(limit));
    for (const key of ['since', 'until', 'card', 'event']) {
      if (filter[key]) params.set(key, filter[key]);
    }
    const query = params.toString();
    const path = query ? `/engineer/audit?${query}` : '/engineer/audit';
    return request('GET', path, null, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  exportAuditCsv(token, masterKey, filter = {}) {
    const params = new URLSearchParams();
    for (const key of ['since', 'until', 'card', 'event']) {
      if (filter[key]) params.set(key, filter[key]);
    }
    const query = params.toString();
    const path = query ? `/engineer/audit/export?${query}` : '/engineer/audit/export';
    const base = CONFIG.API_BASE_URL;
    const headers = {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    };
    return fetch(`${base}${path}`, { headers }).then(async (resp) => {
      if (!resp.ok) {
        let message = `Export failed (HTTP ${resp.status})`;
        try {
          const body = await resp.json();
          if (body && body.message) message = body.message;
        } catch (_) { /* csv endpoint, non-json errors only */ }
        throw new ApiError(message, resp.status);
      }
      return resp.text();
    });
  },
  listCards(token, masterKey) {
    return request('GET', '/engineer/cards', null, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  issueCard(token, masterKey, payload) {
    return request('POST', '/engineer/cards', payload, {
      Authorization: `Bearer ${token}`,
      'X-Master-Key': masterKey
    });
  },
  revokeCard(token, masterKey, cardNumber) {
    return request(
      'POST',
      '/engineer/cards/revoke',
      { cardNumber },
      {
        Authorization: `Bearer ${token}`,
        'X-Master-Key': masterKey
      }
    );
  },
  setCardAccess(token, masterKey, payload) {
    return request('POST', '/engineer/cards/access', payload, {
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
  },
  unlockCard(token, masterKey, cardNumber) {
    return request(
      'POST',
      '/engineer/unlock-card',
      { cardNumber },
      {
        Authorization: `Bearer ${token}`,
        'X-Master-Key': masterKey
      }
    );
  },
  simulateTamper(token, masterKey, restore = false) {
    return request(
      'POST',
      '/engineer/simulate-tamper',
      { restore },
      {
        Authorization: `Bearer ${token}`,
        'X-Master-Key': masterKey
      }
    );
  }
};
