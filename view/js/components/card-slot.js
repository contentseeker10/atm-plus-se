import { FSM_STATES, store } from '../state.js';
import { getTestCardsSnapshot, toggleTestCardExpired } from '../api.js';
import { escapeHtml, maskCardNumber, qs } from '../utils/dom.js';

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
    const cards = getTestCardsSnapshot();
    const isIdle = state.fsmState === FSM_STATES.IDLE && !state.isBusy;

    this._walletRoot.innerHTML = cards
      .map((card) => {
        const isCurrent = state.cardNumber === card.cardNumber;
        const statusTags = [];
        if (card.blocked) statusTags.push('BLOCKED');
        else if (card.expired) statusTags.push('EXPIRED');
        else statusTags.push(`TRIES: ${card.remainingAttempts}/3`);

        const pinInfo = card.blocked
          ? 'STATUS: BLOCKED'
          : `PIN: ${escapeHtml(card.pin)}${card.masterKey ? ` | KEY: ${escapeHtml(card.masterKey)}` : ''}`;

        const expireToggleBtn =
          card.id === 'card-zero'
            ? `<button
                type="button"
                class="card-insert-btn"
                data-toggle-exp="${escapeHtml(card.cardNumber)}"
                ${!isIdle ? 'disabled' : ''}
              >
                Mode: ${card.expired ? 'Expired Card (Switch to $0.00)' : '$0.00 Balance (Switch to Expired)'}
              </button>`
            : '';

        return `
          <article class="test-card-item ${escapeHtml(card.theme)} ${isCurrent ? 'active-card' : ''}">
            <div class="card-top-bar">
              <img src="./assets/chip.svg" alt="" class="card-chip-icon">
              <span class="card-badge-tag">${escapeHtml(card.badge)} // ${escapeHtml(statusTags.join(' '))}</span>
            </div>
            <div class="card-pan-display">${escapeHtml(card.cardNumber)}</div>
            <div class="card-Details-row">
              <span class="card-holder-name">${escapeHtml(card.cardHolder)}</span>
              <span class="card-pin-hint">${pinInfo}</span>
            </div>
            ${expireToggleBtn}
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
      })
      .join('');

    this._walletRoot.querySelectorAll('[data-insert-pan]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const pan = btn.getAttribute('data-insert-pan');
        if (pan && typeof this._onInsertCard === 'function') {
          this._onInsertCard(pan);
        }
      });
    });

    this._walletRoot.querySelectorAll('[data-toggle-exp]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const pan = btn.getAttribute('data-toggle-exp');
        if (pan) {
          toggleTestCardExpired(pan);
          this._renderWallet(store.getState());
        }
      });
    });
  }
}
