import { escapeHtml, formatCents, qs } from '../utils/dom.js';

export class ReceiptComponent {
  constructor({ rootEl, onDismiss }) {
    this._rootEl = rootEl;
    this._onDismiss = onDismiss;
  }

  render(state) {
    if (!this._rootEl) return;
    const receipt = state.lastReceipt;

    if (!receipt) {
      this._rootEl.innerHTML = `
        <div class="receipt-slot-box">
          <div class="receipt-exit-slit" aria-hidden="true"></div>
          <span class="card-slot-caption">NO RECEIPT PRINTED</span>
        </div>
      `;
      return;
    }

    const extraRows = [];
    if (receipt.overdraftCoveredCents > 0) {
      extraRows.push(`
        <div class="receipt-row">
          <span>OVERDRAFT COVER:</span>
          <span>${escapeHtml(formatCents(receipt.overdraftCoveredCents))}</span>
        </div>
      `);
    }
    if (receipt.overflowSweptCents > 0) {
      extraRows.push(`
        <div class="receipt-row">
          <span>OVERFLOW SWEEP:</span>
          <span>${escapeHtml(formatCents(receipt.overflowSweptCents))}</span>
        </div>
      `);
    }

    this._rootEl.innerHTML = `
      <div class="receipt-slot-box">
        <div class="receipt-exit-slit" aria-hidden="true"></div>
        <div class="receipt-paper" role="region" aria-label="Printed Transaction Receipt">
          <div class="receipt-header">
            ATM+ SE // NATIONAL BANK<br>
            TERMINAL #UA-KBP-042
          </div>
          <div class="receipt-row">
            <span>TX ID:</span>
            <span>${escapeHtml(receipt.txId)}</span>
          </div>
          <div class="receipt-row">
            <span>DATE:</span>
            <span>${escapeHtml(receipt.timestamp)}</span>
          </div>
          <div class="receipt-row">
            <span>CARD:</span>
            <span>${escapeHtml(receipt.card)}</span>
          </div>
          <div class="receipt-row">
            <span>TYPE:</span>
            <span>${escapeHtml(receipt.type)}</span>
          </div>
          <div class="receipt-row">
            <span>ACCOUNT:</span>
            <span>${escapeHtml(receipt.account)}</span>
          </div>
          <div class="receipt-row">
            <span>AMOUNT:</span>
            <strong>${escapeHtml(formatCents(receipt.amountCents))}</strong>
          </div>
          ${extraRows.join('')}
          <div class="receipt-row">
            <span>AVAIL BAL:</span>
            <strong>${escapeHtml(formatCents(receipt.newBalanceCents))}</strong>
          </div>
          <div class="receipt-row">
            <span>HMAC SIG:</span>
            <span>${escapeHtml(receipt.authHash)}</span>
          </div>
          <button type="button" id="receipt-dismiss-btn" class="receipt-dismiss-btn">
            Tear Off Receipt
          </button>
        </div>
      </div>
    `;

    const dismissBtn = qs('#receipt-dismiss-btn', this._rootEl);
    if (dismissBtn) {
      dismissBtn.addEventListener('click', () => {
        if (typeof this._onDismiss === 'function') {
          this._onDismiss();
        }
      });
    }
  }
}
