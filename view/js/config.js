const STORAGE_KEY_URL = 'atm_se_api_base_url';

const isSameBackendOrigin =
  (window.location.protocol === 'http:' || window.location.protocol === 'https:') &&
  (window.location.port === '8080' || window.location.port === '');

const defaultApiUrl = isSameBackendOrigin ? '/api' : 'http://localhost:8080/api';

export const CONFIG = {
  API_BASE_URL: localStorage.getItem(STORAGE_KEY_URL) || defaultApiUrl,
  INACTIVITY_TIMEOUT_SEC: 45,
  DENOMINATIONS_CENTS: [1000, 2000, 5000, 10000, 20000, 50000],
  QUICK_WITHDRAW_CENTS: [2000, 5000, 10000, 20000, 50000, 100000]
};

export const DEMO_CARDS = Object.freeze([
  {
    id: 'customer-valid',
    label: 'Customer Valid Card',
    cardNumber: '4111-2222-3333-4444',
    cardHolder: 'IVAN PETRENKO',
    pin: '1234',
    role: 'Customer',
    blocked: false,
    theme: 'theme-customer-blue',
    badge: 'VISA PREMIER',
    accountsHint: 'CHK-4444 ($1,540) / SAV-4444 ($4,500)'
  },
  {
    id: 'card-blocked',
    label: 'Blocked Card',
    cardNumber: '4111-0000-0000-9999',
    cardHolder: 'BLOCKED USER',
    pin: '9999',
    role: 'Customer',
    blocked: true,
    theme: 'theme-blocked-red',
    badge: 'CARD BLOCKED',
    accountsHint: 'CHK-9999 / SAV-9999'
  },
  {
    id: 'card-zero',
    label: 'Zero Balance Card',
    cardNumber: '4111-5555-6666-7777',
    cardHolder: 'OLENA KOVALENKO',
    pin: '4321',
    role: 'Customer',
    blocked: false,
    theme: 'theme-zero-amber',
    badge: 'ZERO BALANCE',
    accountsHint: 'CHK-7777 ($0.00) / SAV-7777 ($0.00)'
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
    theme: 'theme-engineer-black',
    badge: 'MAINTENANCE',
    accountsHint: 'Vault & Audit Diagnostics'
  }
]);

export function updateConfig({ apiBaseUrl }) {
  if (typeof apiBaseUrl === 'string' && apiBaseUrl.trim()) {
    CONFIG.API_BASE_URL = apiBaseUrl.trim().replace(/\/+$/, '');
    localStorage.setItem(STORAGE_KEY_URL, CONFIG.API_BASE_URL);
  }
}
