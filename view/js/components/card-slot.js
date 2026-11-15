import { DEMO_CARDS } from '../config.js';
import { FSM_STATES, store } from '../state.js';
import { escapeHtml, maskCardNumber, qs, wipeInput } from '../utils/dom.js';

export class CardSlotComponent {
  constructor({ slotRoot, walletRoot, ledLabelEl, onInsertCard, onEjectCard }) {
    this._slotRoot = slotRoot;
    this._walletRoot = walletRoot;
    this._ledLabelEl = ledLabelEl;
    this._onInsertCard = onInsertCard;
    this._onEjectCard = onEjectCard;
    this._ledState = 'ready';
    this._ejecting = false;
  }

  setLedState(state) {
    this._ledState = state;
    this.render(store.getState());
  }

  triggerEjectAnimation() {
    this._ejecting = true;
    this.render(store.getState());
    window.setTimeout(() => {
      this._ejecting = false;
      this._ledState = 'ready';
      this.render(store.getState());
    }, 360);
  }

  render(state) {
    this._renderSlot(state);
    this._renderWallet(state);
  }

  _renderSlot(state) {
    if (!this._slotRoot) return;
    const hasCard = state.fsmState !== FSM_STATES.IDLE && Boolean(state.cardNumber);

    let ledClass = '';
    let ledText = 'READY FOR CARD';
    let labelClass = 'led-status-text';

    if (this._ledState === 'error' || state.alert?.type === 'error') {
      ledClass = 'led-error';
      ledText = 'READER ALERT';
      labelClass = 'led-status-text status-error';
    } else if (state.isBusy || hasCard) {
      ledClass = 'led-reading';
      ledText = hasCard ? 'CARD LOCKED' : 'READING...';
      labelClass = 'led-status-text status-reading';
    }

    if (this._ledLabelEl) {
      this._ledLabelEl.className = labelClass;
      this._ledLabelEl.textContent = ledText;
    }

    const tokenClass = this._ejecting
      ? 'inserted-card-token is-ejecting'
      : hasCard
        ? 'inserted-card-token is-inserted'
        : 'inserted-card-token';

    this._slotRoot.innerHTML = `
      <div class="card-slot-assembly">
        <div class="card-throat-housing">
          <div class="card-led-bar ${ledClass}"></div>
          <div class="card-slot-slit" aria-hidden="true"></div>
          <div class="${tokenClass}">
            <span>${escapeHtml(state.maskedPan || maskCardNumber(state.cardNumber))}</span>
            <span>CHIP</span>
          </div>
        </div>
        <div class="card-slot-actions">
          <span class="card-slot-caption">
            ${hasCard ? `HOLDER: ${escapeHtml(state.cardHolder || 'ACTIVE')}` : 'INSERT SMART CARD TO BEGIN'}
          </span>
          <button
            type="button"
            id="slot-eject-btn"
            class="screen-btn screen-btn-danger"
            ${!hasCard || state.isBusy ? 'disabled' : ''}
          >
            Eject
          </button>
        </div>
      </div>
    `;

    const ejectBtn = qs('#slot-eject-btn', this._slotRoot);
    if (ejectBtn) {
      ejectBtn.addEventListener('click', () => {
        if (typeof this._onEjectCard === 'function') {
          this._onEjectCard();
        }
      });
    }
  }

  _renderWallet(state) {
    if (!this._walletRoot) return;
    const isIdle = state.fsmState === FSM_STATES.IDLE && !state.isBusy;

    const cardsHtml = DEMO_CARDS.map((card) => {
      const override = state.cardStatusOverrides?.[card.cardNumber] || {};
      const isBlocked =
        override.blocked !== undefined ? Boolean(override.blocked) : Boolean(card.blocked);
      const accountsHint =
        override.accountsHint !== undefined ? override.accountsHint : card.accountsHint;
      const themeClass = isBlocked
        ? 'theme-blocked-red'
        : card.blocked
          ? 'theme-customer-blue'
          : card.theme;
      const badgeLabel = isBlocked
        ? 'CARD BLOCKED'
        : card.blocked
          ? 'UNLOCKED CARD'
          : card.badge;
      const isCurrent = state.cardNumber === card.cardNumber;
      const statusTag = isBlocked ? 'BLOCKED' : card.role.toUpperCase();
      const pinInfo = isBlocked
        ? 'STATUS: BLOCKED'
        : `PIN: ${escapeHtml(card.pin)}${card.masterKey ? ` | KEY: ${escapeHtml(card.masterKey)}` : ''}`;

      return `
        <article class="test-card-item ${escapeHtml(themeClass)} ${isCurrent ? 'active-card' : ''}">
          <div class="card-top-bar">
            <img src="./assets/chip.svg" alt="" class="card-chip-icon">
            <span class="card-badge-tag">${escapeHtml(badgeLabel)} // ${escapeHtml(statusTag)}</span>
          </div>
          <div class="card-pan-display">${escapeHtml(card.cardNumber)}</div>
          <div class="card-Details-row">
            <span class="card-holder-name">${escapeHtml(card.cardHolder)}</span>
            <span class="card-pin-hint">${pinInfo}</span>
          </div>
          ${accountsHint ? `<div class="card-Details-row"><span class="card-slot-caption" style="color: rgba(255,255,255,0.78);">${escapeHtml(accountsHint)}</span></div>` : ''}
          <button
            type="button"
            class="card-insert-btn"
            data-insert-pan="${escapeHtml(card.cardNumber)}"
            ${!isIdle ? 'disabled' : ''}
          >
            ${isCurrent ? 'Card Currently Inserted' : 'Insert Card into ATM'}
          </button>
        </article>
      `;
    }).join('');

    const customPanFormHtml = `
      <form id="custom-pan-form" class="hardware-module" style="gap:8px;">
        <label for="custom-pan-input" class="module-title">CUSTOM CARD PAN</label>
        <div style="display:flex; gap:6px;">
          <input
            type="text"
            id="custom-pan-input"
            class="screen-input"
            placeholder="e.g. 4111-0000-1111-2222"
            autocomplete="off"
            ${!isIdle ? 'disabled' : ''}
            required
          >
          <button type="submit" class="screen-btn" ${!isIdle ? 'disabled' : ''}>
            Insert
          </button>
        </div>
      </form>
    `;

    this._walletRoot.innerHTML = `${cardsHtml}${customPanFormHtml}`;

    this._walletRoot.querySelectorAll('[data-insert-pan]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const pan = btn.getAttribute('data-insert-pan');
        if (pan && typeof this._onInsertCard === 'function') {
          this._onInsertCard(pan);
        }
      });
    });

    const customForm = qs('#custom-pan-form', this._walletRoot);
    const customInput = qs('#custom-pan-input', this._walletRoot);
    if (customForm && customInput) {
      customForm.addEventListener('submit', (e) => {
        e.preventDefault();
        const pan = customInput.value.trim();
        wipeInput(customInput);
        if (pan && typeof this._onInsertCard === 'function') {
          this._onInsertCard(pan);
        }
      });
    }
  }
}
