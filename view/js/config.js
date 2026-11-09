const STORAGE_KEY_MOCK = 'atm_se_use_mock';
const STORAGE_KEY_URL = 'atm_se_api_base_url';

const defaultApiUrl = window.location.origin.includes('github.io') || window.location.protocol === 'file:'
  ? 'http://localhost:8080/api'
  : '/api';

export const CONFIG = {
  USE_MOCK: localStorage.getItem(STORAGE_KEY_MOCK) !== null
    ? localStorage.getItem(STORAGE_KEY_MOCK) === 'true'
    : true,
  API_BASE_URL: localStorage.getItem(STORAGE_KEY_URL) || defaultApiUrl,
  INACTIVITY_TIMEOUT_SEC: 45,
  MOCK_LATENCY_MIN_MS: 160,
  MOCK_LATENCY_MAX_MS: 280,
  DENOMINATIONS_CENTS: [1000, 2000, 5000, 10000, 20000, 50000],
  QUICK_WITHDRAW_CENTS: [2000, 5000, 10000, 20000, 50000, 100000]
};

export function updateConfig({ useMock, apiBaseUrl }) {
  if (typeof useMock === 'boolean') {
    CONFIG.USE_MOCK = useMock;
    localStorage.setItem(STORAGE_KEY_MOCK, String(useMock));
  }
  if (typeof apiBaseUrl === 'string' && apiBaseUrl.trim()) {
    CONFIG.API_BASE_URL = apiBaseUrl.trim().replace(/\/+$/, '');
    localStorage.setItem(STORAGE_KEY_URL, CONFIG.API_BASE_URL);
  }
}
