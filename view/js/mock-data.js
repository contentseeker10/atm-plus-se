export const INITIAL_TEST_CARDS = [
  {
    id: 'customer-valid',
    label: 'Customer Valid Card',
    cardNumber: '4111-2222-3333-4444',
    cardHolder: 'IVAN PETRENKO',
    pin: '1234',
    role: 'Customer',
    blocked: false,
    expired: false,
    remainingAttempts: 3,
    theme: 'theme-customer-blue',
    badge: 'VISA PREMIER',
    accounts: [
      { id: 'CHK-4444', type: 'Checking', balance: 154000, clientMin: 10000, overflowMax: 500000 },
      { id: 'SAV-4444', type: 'Savings', balance: 450000 }
    ]
  },
  {
    id: 'card-blocked',
    label: 'Blocked Card',
    cardNumber: '4111-0000-0000-9999',
    cardHolder: 'BLOCKED USER',
    pin: '9999',
    role: 'Customer',
    blocked: true,
    expired: false,
    remainingAttempts: 0,
    theme: 'theme-blocked-red',
    badge: 'CARD BLOCKED',
    accounts: [
      { id: 'CHK-9999', type: 'Checking', balance: 82000, clientMin: 10000, overflowMax: 500000 },
      { id: 'SAV-9999', type: 'Savings', balance: 120000 }
    ]
  },
  {
    id: 'card-zero',
    label: 'Zero Balance / Expired',
    cardNumber: '4111-5555-6666-7777',
    cardHolder: 'OLENA KOVALENKO',
    pin: '4321',
    role: 'Customer',
    blocked: false,
    expired: false,
    remainingAttempts: 3,
    theme: 'theme-zero-amber',
    badge: 'ZERO BAL / EXP',
    accounts: [
      { id: 'CHK-7777', type: 'Checking', balance: 0, clientMin: 0, overflowMax: 500000 },
      { id: 'SAV-7777', type: 'Savings', balance: 0 }
    ]
  },
  {
    id: 'engineer-card',
    label: 'Engineer Service Card',
    cardNumber: '9999-8888-7777-0001',
    cardHolder: 'SERVICE ENGINEER #42',
    pin: '0000',
    masterKey: 'ENG-MASTER-KEY-2026',
    role: 'Engineer',
    blocked: false,
    expired: false,
    remainingAttempts: 3,
    theme: 'theme-engineer-black',
    badge: 'MAINTENANCE',
    accounts: []
  }
];

export const INITIAL_VAULT_STATE = {
  doorOpen: false,
  masterKey: 'ENG-MASTER-KEY-2026',
  hardwareCounters: {
    dispenseCycles: 14,
    depositCycles: 6,
    jamEvents: 0
  },
  cassettes: [
    { denom: 1000, count: 100, capacity: 2000 },
    { denom: 2000, count: 200, capacity: 2000 },
    { denom: 5000, count: 200, capacity: 2000 },
    { denom: 10000, count: 300, capacity: 2000 },
    { denom: 20000, count: 150, capacity: 2000 },
    { denom: 50000, count: 100, capacity: 2000 }
  ]
};

export function computeRecordHash(prevHash, timestamp, event, detail, amountCents = 0) {
  const payload = `${prevHash}|${timestamp}|${event}|${detail}|${amountCents}|ATM_SE_HMAC_2026`;
  let h1 = 0xdeadbeef ^ payload.length;
  let h2 = 0x41c6ce57 ^ payload.length;
  for (let i = 0; i < payload.length; i += 1) {
    const ch = payload.charCodeAt(i);
    h1 = Math.imul(h1 ^ ch, 2654435761);
    h2 = Math.imul(h2 ^ ch, 1597334677);
  }
  h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
  h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
  const part1 = (h1 >>> 0).toString(16).padStart(8, '0');
  const part2 = (h2 >>> 0).toString(16).padStart(8, '0');
  const part3 = ((h1 ^ h2) >>> 0).toString(16).padStart(8, '0');
  const part4 = ((Math.imul(h1, 31) ^ h2) >>> 0).toString(16).padStart(8, '0');
  return `${part1}${part2}${part3}${part4}${part1}${part2}${part3}${part4}`;
}

export function buildInitialAuditLog() {
  const genesis = '0000000000000000000000000000000000000000000000000000000000000000';
  const seeds = [
    { timestamp: '2026-11-09T09:00:00Z', event: 'SystemBoot', detail: 'HSM & CashVault initialized', amountCents: 12500000 },
    { timestamp: '2026-11-09T09:15:22Z', event: 'Deposit', detail: 'CHK-4444<-ATM_VAULT', amountCents: 50000 },
    { timestamp: '2026-11-09T10:04:11Z', event: 'Withdrawal', detail: 'CHK-4444->ATM_VAULT', amountCents: 20000 },
    { timestamp: '2026-11-09T11:30:45Z', event: 'Transfer', detail: 'CHK-4444->SAV-4444', amountCents: 15000 }
  ];

  const records = [];
  let prevHash = genesis;
  seeds.forEach((seed, idx) => {
    const hash = computeRecordHash(prevHash, seed.timestamp, seed.event, seed.detail, seed.amountCents);
    records.push({
      id: idx + 1,
      timestamp: seed.timestamp,
      event: seed.event,
      detail: seed.detail,
      amountCents: seed.amountCents,
      prevHash,
      hash,
      tampered: false
    });
    prevHash = hash;
  });
  return records;
}
