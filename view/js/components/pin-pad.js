import { FSM_STATES, store } from '../state.js';

const KEYPAD_LAYOUT = [
  { key: '1', sub: '' },
  { key: '2', sub: 'ABC' },
  { key: '3', sub: 'DEF' },
  { key: 'CANCEL', label: 'CANCEL', cls: 'pin-key-action pin-key-cancel' },
  { key: '4', sub: 'GHI' },
  { key: '5', sub: 'JKL' },
  { key: '6', sub: 'MNO' },
  { key: 'CLEAR', label: 'CLEAR', cls: 'pin-key-action pin-key-clear' },
  { key: '7', sub: 'PQRS' },
  { key: '8', sub: 'TUV' },
  { key: '9', sub: 'WXYZ' },
  { key: 'ENTER', label: 'ENTER', cls: 'pin-key-action pin-key-enter' },
  { key: '00', sub: '' },
  { key: '0', sub: '+' },
  { key: '000', sub: '' },
  { key: 'NOP', label: 'EPP', cls: 'pin-key-action', disabled: true }
];

export class PinPadComponent {
  #pinBuffer = '';

  constructor({ rootEl, onSubmitPin, onCancel, onNumericInput }) {
    this._rootEl = rootEl;
    this._onSubmitPin = onSubmitPin;
    this._onCancel = onCancel;
    this._onNumericInput = onNumericInput;
    this._maxPinLength = 4;

    this._renderPad();
    this._bindKeyboard();
  }

  clearBuffer() {
    this.#pinBuffer = '';
    if (store.getState().pinLength !== 0) {
      store.setState({ pinLength: 0 });
    }
  }

  _renderPad() {
    if (!this._rootEl) return;
    this._rootEl.innerHTML = `
      <div class="pinpad-grid" role="group" aria-label="Encrypted PIN Keypad">
        ${KEYPAD_LAYOUT.map(
          (item) => `
            <button
              type="button"
              class="pin-key ${item.cls || ''}"
              data-pad-key="${item.key}"
              ${item.disabled ? 'disabled' : ''}
            >
              <span>${item.label || item.key}</span>
              ${item.sub ? `<span class="pin-key-sub">${item.sub}</span>` : ''}
            </button>
          `
        ).join('')}
      </div>
    `;

    this._rootEl.querySelectorAll('[data-pad-key]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const key = btn.getAttribute('data-pad-key');
        if (key && key !== 'NOP') {
          this.handleKey(key);
        }
      });
    });
  }

  _bindKeyboard() {
    window.addEventListener('keydown', (event) => {
      const activeTag = document.activeElement?.tagName;
      const isTextInput = activeTag === 'INPUT' || activeTag === 'SELECT' || activeTag === 'TEXTAREA';

      if (isTextInput) {
        if (event.key === 'Escape') {
          event.preventDefault();
          this.handleKey('CANCEL');
        }
        return;
      }

      if (/^[0-9]$/.test(event.key)) {
        event.preventDefault();
        this._flashKey(event.key);
        this.handleKey(event.key);
      } else if (event.key === 'Backspace' || event.key === 'Delete') {
        event.preventDefault();
        this._flashKey('CLEAR');
        this.handleKey('CLEAR');
      } else if (event.key === 'Enter') {
        event.preventDefault();
        this._flashKey('ENTER');
        this.handleKey('ENTER');
      } else if (event.key === 'Escape') {
        event.preventDefault();
        this._flashKey('CANCEL');
        this.handleKey('CANCEL');
      }
    });
  }

  _flashKey(key) {
    if (!this._rootEl) return;
    const btn = this._rootEl.querySelector(`[data-pad-key="${key}"]`);
    if (!btn) return;
    btn.classList.add('active-press');
    window.setTimeout(() => btn.classList.remove('active-press'), 120);
  }

  handleKey(key) {
    const state = store.getState();
    if (state.isBusy) return;

    if (key === 'CANCEL') {
      this.clearBuffer();
      if (typeof this._onCancel === 'function') {
        this._onCancel();
      }
      return;
    }

    if (state.fsmState === FSM_STATES.CARD_INSERTED) {
      if (key === 'CLEAR') {
        this.#pinBuffer = this.#pinBuffer.slice(0, -1);
        store.setState({ pinLength: this.#pinBuffer.length, alert: null });
        return;
      }

      if (key === 'ENTER') {
        if (this.#pinBuffer.length < 4) {
          store.setState({
            alert: { type: 'warn', message: 'Please enter a 4-digit PIN before pressing ENTER.' }
          });
          return;
        }
        const pinToSubmit = this.#pinBuffer;
        this.clearBuffer();
        if (typeof this._onSubmitPin === 'function') {
          this._onSubmitPin(pinToSubmit);
        }
        return;
      }

      if (/^[0-9]+$/.test(key)) {
        const digit = key[0];
        if (this.#pinBuffer.length < this._maxPinLength) {
          this.#pinBuffer += digit;
          store.setState({ pinLength: this.#pinBuffer.length, alert: null });
        }
      }
      return;
    }

    if (typeof this._onNumericInput === 'function') {
      this._onNumericInput(key);
    }
  }
}
