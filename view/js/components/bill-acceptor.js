import { CONFIG } from '../config.js';
import { CUSTOMER_VIEWS, FSM_STATES } from '../state.js';
import { escapeHtml, formatCents, formatDenomLabel, qs } from '../utils/dom.js';

export class BillAcceptorComponent {
  constructor({ rootEl, shutterLabelEl, onDepositConfirm, onCollectDispensed, onStagedChange }) {
    this._rootEl = rootEl;
    this._shutterLabelEl = shutterLabelEl;
    this._onDepositConfirm = onDepositConfirm;
    this._onCollectDispensed = onCollectDispensed;
    this._onStagedChange = onStagedChange;
    this._stagedBills = [];
  }

  getStagedBills() {
    return [...this._stagedBills];
  }

  addStagedBill(denomCents) {
    if (this._stagedBills.length >= 50) return;
    this._stagedBills.push(Number(denomCents));
    this._notifyStaged();
  }

  clearStagedBills() {
    this._stagedBills = [];
    this._notifyStaged();
  }

  _notifyStaged() {
    if (typeof this._onStagedChange === 'function') {
      this._onStagedChange(this.getStagedBills());
    }
  }

  render(state) {
    if (!this._rootEl) return;

    const isDepositMode =
      state.fsmState === FSM_STATES.CUSTOMER_SESSION &&
      state.customerView === CUSTOMER_VIEWS.DEPOSIT;
    const hasDispensed =
      Array.isArray(state.dispensedBills) && state.dispensedBills.length > 0;
    const isOpen = isDepositMode || hasDispensed;

    if (this._shutterLabelEl) {
      this._shutterLabelEl.textContent = hasDispensed
        ? 'SHUTTER OPEN // COLLECT CASH'
        : isDepositMode
          ? 'SHUTTER OPEN // FEED NOTES'
          : 'SHUTTER CLOSED';
    }

    const countsMap = new Map();
    for (const denom of this._stagedBills) {
      countsMap.set(denom, (countsMap.get(denom) || 0) + 1);
    }
    const stagedTotal = this._stagedBills.reduce((sum, b) => sum + b, 0);

    let trayContentHtml = `<span class="card-slot-caption">MOTORIZED SHUTTER LOCKED</span>`;

    if (hasDispensed) {
      const chips = state.dispensedBills
        .map(
          (item) => `
            <span class="banknote-chip">
              ${escapeHtml(item.count)} x ${escapeHtml(formatDenomLabel(item.denomination))}
            </span>
          `
        )
        .join('');
      trayContentHtml = `
        <div class="dispensed-stack">
          ${chips}
          <button type="button" id="tray-collect-cash-btn" class="screen-btn screen-btn-primary">
            Take Cash
          </button>
        </div>
      `;
    } else if (isDepositMode) {
      if (this._stagedBills.length === 0) {
        trayContentHtml = `<span class="card-slot-caption">SELECT BANKNOTES BELOW TO INSERT INTO ACCEPTOR</span>`;
      } else {
        const chips = Array.from(countsMap.entries())
          .sort((a, b) => a[0] - b[0])
          .map(
            ([denom, count]) => `
              <span class="banknote-chip">
                ${escapeHtml(count)} x ${escapeHtml(formatDenomLabel(denom))}
              </span>
            `
          )
          .join('');
        trayContentHtml = `<div class="dispensed-stack">${chips}</div>`;
      }
    }

    const depositControlsHtml = isDepositMode
      ? `
        <div class="deposit-controls">
          <div class="denom-picker-grid">
            ${CONFIG.DENOMINATIONS_CENTS.map(
              (denom) => `
                <button
                  type="button"
                  class="denom-add-btn"
                  data-add-bill="${denom}"
                  ${state.isBusy ? 'disabled' : ''}
                >
                  +${formatDenomLabel(denom)}
                </button>
              `
            ).join('')}
          </div>
          <div class="card-slot-actions">
            <span class="card-slot-caption">
              STAGED: ${this._stagedBills.length} NOTES (${formatCents(stagedTotal)})
            </span>
            <div style="display:flex; gap:6px;">
              <button
                type="button"
                id="tray-clear-bills-btn"
                class="screen-btn"
                ${this._stagedBills.length === 0 || state.isBusy ? 'disabled' : ''}
              >
                Clear
              </button>
              <button
                type="button"
                id="tray-deposit-submit-btn"
                class="screen-btn screen-btn-primary"
                ${this._stagedBills.length === 0 || state.isBusy ? 'disabled' : ''}
              >
                Deposit ${formatCents(stagedTotal)}
              </button>
            </div>
          </div>
        </div>
      `
      : '';

    this._rootEl.innerHTML = `
      <div class="cash-acceptor-box">
        <div class="shutter-throat ${isOpen ? 'is-open' : ''}">
          <div class="shutter-door" aria-hidden="${isOpen}">
            <span>ARMORED SHUTTER // CLOSED</span>
          </div>
          ${trayContentHtml}
        </div>
        ${depositControlsHtml}
      </div>
    `;

    this._rootEl.querySelectorAll('[data-add-bill]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const denom = Number(btn.getAttribute('data-add-bill'));
        this.addStagedBill(denom);
        this.render(state);
      });
    });

    const clearBtn = qs('#tray-clear-bills-btn', this._rootEl);
    if (clearBtn) {
      clearBtn.addEventListener('click', () => {
        this.clearStagedBills();
        this.render(state);
      });
    }

    const submitBtn = qs('#tray-deposit-submit-btn', this._rootEl);
    if (submitBtn) {
      submitBtn.addEventListener('click', () => {
        if (typeof this._onDepositConfirm === 'function') {
          this._onDepositConfirm(this.getStagedBills());
        }
      });
    }

    const collectBtn = qs('#tray-collect-cash-btn', this._rootEl);
    if (collectBtn) {
      collectBtn.addEventListener('click', () => {
        if (typeof this._onCollectDispensed === 'function') {
          this._onCollectDispensed();
        }
      });
    }
  }
}
