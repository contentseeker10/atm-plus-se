const HTML_ESCAPE_MAP = {
  '&': '&amp;',
  '<': '&lt;',
  '>': '&gt;',
  '"': '&quot;',
  "'": '&#39;',
  '/': '&#x2F;',
  '`': '&#x60;',
  '=': '&#x3D;'
};

export function escapeHtml(value) {
  if (value === null || value === undefined) return '';
  return String(value).replace(/[&<>"'`=/]/g, (char) => HTML_ESCAPE_MAP[char]);
}

export function formatCents(cents) {
  const numeric = Number(cents) || 0;
  const sign = numeric < 0 ? '-' : '';
  const abs = Math.abs(numeric);
  const dollars = Math.floor(abs / 100);
  const remainder = abs % 100;
  return `${sign}$${dollars.toLocaleString('en-US')}.${String(remainder).padStart(2, '0')}`;
}

export function formatDenomLabel(denomCents) {
  return `$${Math.floor(Number(denomCents) / 100)}`;
}

export function maskCardNumber(pan) {
  if (!pan) return '****-****-****-****';
  const digits = String(pan).replace(/\D/g, '');
  if (digits.length < 8) return escapeHtml(pan);
  return `${digits.slice(0, 4)}-****-****-${digits.slice(-4)}`;
}

export function shortHash(hash, len = 10) {
  if (!hash) return 'GENESIS';
  const clean = String(hash);
  if (clean.length <= len * 2) return escapeHtml(clean);
  return `${escapeHtml(clean.slice(0, len))}...${escapeHtml(clean.slice(-6))}`;
}

export function setText(element, text) {
  if (!element) return;
  element.textContent = text ?? '';
}

export function wipeInput(inputEl) {
  if (!inputEl) return;
  inputEl.value = '';
  inputEl.setAttribute('value', '');
}

export function qs(selector, scope = document) {
  return scope.querySelector(selector);
}

export function qsa(selector, scope = document) {
  return Array.from(scope.querySelectorAll(selector));
}
