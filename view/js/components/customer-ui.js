import { CONFIG } from '../config.js';
import { CUSTOMER_VIEWS, FSM_STATES, store } from '../state.js';
import {
  escapeHtml,
  formatCents,
  formatDenomLabel,
  maskCardNumber,
  qs,
  wipeInput
} from '../utils/dom.js';

export class CustomerUiComponent {
  constructor({
    viewportEl,
    actions
  }) {
    this._viewportEl = viewportEl;
    this._actions = actions;
    this._pendingReceipt = null;
  }

  setPendingReceipt(receipt) {
    this._pendingReceipt = receipt;
  }

  render(state) {
    if (!this._viewportEl) return;

    if (state.fsmState === FSM_STATES.IDLE) {
      this._renderIdle(state);
      return;
    }

    if (state.fsmState === FSM_STATES.CARD_INSERTED) {
      this._renderPinEntry(state);
      return;
    }

    if (state.fsmState === FSM_STATES.CUSTOMER_SESSION) {
      switch (state.customerView) {
        case CUSTOMER_VIEWS.BALANCE:
          this._renderBalance(state);
          break;
        case CUSTOMER_VIEWS.WITHDRAW:
          this._renderWithdraw(state);
          break;
        case CUSTOMER_VIEWS.DEPOSIT:
          this._renderDeposit(state);
          break;
        case CUSTOMER_VIEWS.TRANSFER:
          this._renderTransfer(state);
          break;
        case CUSTOMER_VIEWS.DISPENSING:
          this._renderDispensing(state);
          break;
        case CUSTOMER_VIEWS.COMPLETE:
          this._renderComplete(state);
          break;
        case CUSTOMER_VIEWS.MENU:
        default:
          this._renderMenu(state);
          break;
      }
    }
  }

  _renderIdle() {
    store.setSideKeys({});
    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="idle-hero">
          <div class="idle-pulse-badge">
            <span>READER READY // INSERT CARD</span>
          </div>
          <h2 class="screen-title">WELCOME TO ATM+ SE</h2>
          <p class="screen-subtitle">
            Select a smart card from the right-hand Test Card Wallet to begin a Customer or Maintenance session.
          </p>
          <div class="account-meta">
            DOUBLE-ENTRY LEDGER // SHA-256 + HMAC AUDIT CHAIN // ZERO-TRUST TERMINAL
          </div>
        </div>
      </div>
    `;
  }

  _renderPinEntry(state) {
    store.setSideKeys({
      F8: {
        label: 'Cancel / Eject',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const slotsHtml = [0, 1, 2, 3]
      .map((idx) => {
        const filled = idx < state.pinLength;
        return `<div class="pin-slot-char ${filled ? 'filled' : ''}">${filled ? '*' : ''}</div>`;
      })
      .join('');

    const attemptNumber = Math.min(3, Math.max(1, 4 - (state.remainingAttempts || 3)));

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">AUTHENTICATION REQUIRED</h2>
          <p class="screen-subtitle">
            Enter your 4-digit PIN using the Encrypted PIN Pad or keyboard and press ENTER.
          </p>
        </div>

        <div class="pin-entry-box">
          <div class="account-type-label">
            CARD: ${escapeHtml(state.maskedPan || maskCardNumber(state.cardNumber))}
            ${state.cardHolder ? ` // ${escapeHtml(state.cardHolder)}` : ''}
          </div>
          <div class="pin-mask-slots" aria-label="PIN Masked Input">
            ${slotsHtml}
          </div>
          <div class="attempts-badge">
            ATTEMPT ${attemptNumber} OF 3 (REMAINING TRIES: ${escapeHtml(state.remainingAttempts)})
          </div>
        </div>
      </div>
    `;
  }

  _renderMenu(state) {
    const nextAccType = state.selectedAccountType === 'Checking' ? 'Savings' : 'Checking';

    store.setSideKeys({
      F1: {
        label: 'Balance Inquiry',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.BALANCE, alert: null })
      },
      F2: {
        label: 'Withdraw Cash',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.WITHDRAW, alert: null })
      },
      F3: {
        label: 'Deposit Cash',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.DEPOSIT, alert: null })
      },
      F4: {
        label: 'Transfer Funds',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.TRANSFER, alert: null })
      },
      F5: {
        label: `Use ${nextAccType}`,
        action: () => store.setState({ selectedAccountType: nextAccType, alert: null })
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const accountsHtml = state.accounts
      .map((acc) => {
        const isSelected = acc.type === state.selectedAccountType;
        const metaText =
          acc.type === 'Checking'
            ? `ID: ${escapeHtml(acc.id || 'CHK')} | Min: ${formatCents(acc.clientMin || 0)}`
            : `ID: ${escapeHtml(acc.id || 'SAV')} | Linked Overdraft Reserve`;
        return `
          <div class="account-card ${isSelected ? 'selected' : ''}" data-select-acc="${escapeHtml(acc.type)}">
            <span class="account-type-label">${escapeHtml(acc.type)} ${isSelected ? '[ACTIVE]' : ''}</span>
            <span class="account-balance-value">${escapeHtml(formatCents(acc.balance))}</span>
            <span class="account-meta">${metaText}</span>
          </div>
        `;
      })
      .join('');

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">SELECT BANKING SERVICE</h2>
          <p class="screen-subtitle">
            Welcome, ${escapeHtml(state.cardHolder || 'VALUED CUSTOMER')}. Choose an operation using the side keys.
          </p>
        </div>
        <div class="accounts-grid">
          ${accountsHtml}
        </div>
      </div>
    `;

    this._viewportEl.querySelectorAll('[data-select-acc]').forEach((cardEl) => {
      cardEl.addEventListener('click', () => {
        const type = cardEl.getAttribute('data-select-acc');
        if (type) {
          store.setState({ selectedAccountType: type, alert: null });
        }
      });
    });
  }

  _renderBalance(state) {
    const activeAcc =
      state.accounts.find((a) => a.type === state.selectedAccountType) || state.accounts[0];

    store.setSideKeys({
      F1: {
        label: 'Withdraw Cash',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.WITHDRAW, alert: null })
      },
      F2: {
        label: 'Deposit Cash',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.DEPOSIT, alert: null })
      },
      F6: {
        label: 'Print Receipt',
        variant: 'primary',
        action: () => {
          if (!activeAcc) return;
          store.setState({
            lastReceipt: {
              txId: `INQ-${Date.now().toString().slice(-5)}`,
              timestamp: new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
              type: 'BALANCE INQUIRY',
              card: state.maskedPan || maskCardNumber(state.cardNumber),
              account: activeAcc.type,
              amountCents: 0,
              newBalanceCents: activeAcc.balance,
              authHash: 'VERIFIED-INQUIRY'
            },
            alert: { type: 'success', message: 'Balance inquiry receipt printed to slot.' }
          });
        }
      },
      F7: {
        label: 'Main Menu',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null })
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const cardsHtml = state.accounts
      .map((acc) => {
        const policyLine =
          acc.type === 'Checking'
            ? `Min Reserve: ${formatCents(acc.clientMin || 0)} | Overflow Sweep Cap: ${formatCents(acc.overflowMax || 500000)}`
            : 'Automatically covers Checking overdrafts & receives overflow sweeps';
        return `
          <div class="account-card ${acc.type === state.selectedAccountType ? 'selected' : ''}">
            <span class="account-type-label">${escapeHtml(acc.type)} Account (${escapeHtml(acc.id || acc.type)})</span>
            <span class="account-balance-value">${escapeHtml(formatCents(acc.balance))}</span>
            <span class="account-meta">${escapeHtml(policyLine)}</span>
          </div>
        `;
      })
      .join('');

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">ACCOUNT BALANCE SUMMARY</h2>
          <p class="screen-subtitle">All balances verified in real time against the double-entry ledger.</p>
        </div>
        <div class="accounts-grid">
          ${cardsHtml}
        </div>
      </div>
    `;
  }

  _renderWithdraw(state) {
    const activeAcc =
      state.accounts.find((a) => a.type === state.selectedAccountType) || state.accounts[0];
    const nextAccType = state.selectedAccountType === 'Checking' ? 'Savings' : 'Checking';

    store.setSideKeys({
      F1: {
        label: 'Withdraw $20',
        action: () => this._actions.withdraw(state.selectedAccountType, 2000)
      },
      F2: {
        label: 'Withdraw $50',
        action: () => this._actions.withdraw(state.selectedAccountType, 5000)
      },
      F3: {
        label: 'Withdraw $100',
        action: () => this._actions.withdraw(state.selectedAccountType, 10000)
      },
      F4: {
        label: 'Withdraw $200',
        action: () => this._actions.withdraw(state.selectedAccountType, 20000)
      },
      F5: {
        label: 'Withdraw $500',
        action: () => this._actions.withdraw(state.selectedAccountType, 50000)
      },
      F6: {
        label: `Source: ${nextAccType}`,
        action: () => store.setState({ selectedAccountType: nextAccType, alert: null })
      },
      F7: {
        label: 'Main Menu',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null })
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">CASH WITHDRAWAL // ${escapeHtml(state.selectedAccountType)}</h2>
          <p class="screen-subtitle">
            Available in ${escapeHtml(state.selectedAccountType)}:
            <strong>${escapeHtml(formatCents(activeAcc?.balance || 0))}</strong>
            (Denominations: $10, $20, $50, $100, $200, $500)
          </p>
        </div>

        <div class="quick-amounts-grid">
          ${CONFIG.QUICK_WITHDRAW_CENTS.map(
            (cents) => `
              <button
                type="button"
                class="screen-btn"
                data-quick-withdraw="${cents}"
                ${state.isBusy ? 'disabled' : ''}
              >
                ${formatCents(cents)}
              </button>
            `
          ).join('')}
        </div>

        <form id="custom-withdraw-form" class="screen-form-row">
          <label for="withdraw-dollars-input" class="screen-label">
            CUSTOM AMOUNT IN USD (MULTIPLES OF $10):
          </label>
          <div style="display:flex; gap:8px;">
            <input
              type="number"
              id="withdraw-dollars-input"
              class="screen-input"
              min="10"
              step="10"
              placeholder="e.g. 150"
              required
            >
            <button type="submit" class="screen-btn screen-btn-primary" ${state.isBusy ? 'disabled' : ''}>
              Dispense
            </button>
          </div>
        </form>
      </div>
    `;

    this._viewportEl.querySelectorAll('[data-quick-withdraw]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const cents = Number(btn.getAttribute('data-quick-withdraw'));
        this._actions.withdraw(state.selectedAccountType, cents);
      });
    });

    const form = qs('#custom-withdraw-form', this._viewportEl);
    const input = qs('#withdraw-dollars-input', this._viewportEl);
    if (form && input) {
      form.addEventListener('submit', (e) => {
        e.preventDefault();
        const dollars = Number(input.value);
        wipeInput(input);
        if (!Number.isFinite(dollars) || dollars <= 0 || dollars % 10 !== 0) {
          store.setState({
            alert: { type: 'error', message: 'Withdrawal amount must be a positive multiple of $10.' }
          });
          return;
        }
        this._actions.withdraw(state.selectedAccountType, Math.round(dollars * 100));
      });
    }
  }

  _renderDeposit(state) {
    const stagedBills = this._actions.getStagedBills();
    const stagedTotal = stagedBills.reduce((sum, b) => sum + b, 0);
    const nextAccType = state.selectedAccountType === 'Checking' ? 'Savings' : 'Checking';

    store.setSideKeys({
      F1: {
        label: '+$20 Note',
        action: () => this._actions.addDepositBill(2000)
      },
      F2: {
        label: '+$50 Note',
        action: () => this._actions.addDepositBill(5000)
      },
      F3: {
        label: '+$100 Note',
        action: () => this._actions.addDepositBill(10000)
      },
      F4: {
        label: '+$200 Note',
        action: () => this._actions.addDepositBill(20000)
      },
      F5: {
        label: 'Confirm Deposit',
        variant: 'primary',
        action: () => this._actions.submitDeposit(state.selectedAccountType, stagedBills)
      },
      F6: {
        label: `Target: ${nextAccType}`,
        action: () => store.setState({ selectedAccountType: nextAccType, alert: null })
      },
      F7: {
        label: 'Main Menu',
        action: () => {
          this._actions.clearDepositBills();
          store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null });
        }
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => {
          this._actions.clearDepositBills();
          this._actions.ejectCard();
        }
      }
    });

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">CASH DEPOSIT // ${escapeHtml(state.selectedAccountType)}</h2>
          <p class="screen-subtitle">
            Insert banknotes using the Bill Acceptor shutter below or quick-add buttons.
            Checking balances above $5,000.00 automatically sweep excess to Savings.
          </p>
        </div>

        <div class="account-card selected">
          <span class="account-type-label">STAGED IN BILL ACCEPTOR (${stagedBills.length} NOTES)</span>
          <span class="account-balance-value">${escapeHtml(formatCents(stagedTotal))}</span>
          <span class="account-meta">
            ${
              stagedBills.length > 0
                ? stagedBills.map((d) => formatDenomLabel(d)).join(' + ')
                : 'No bills inserted yet. Click denomination buttons below.'
            }
          </span>
        </div>

        <div style="display:flex; gap:8px; justify-content:flex-end;">
          <button
            type="button"
            id="screen-clear-deposit-btn"
            class="screen-btn"
            ${stagedBills.length === 0 || state.isBusy ? 'disabled' : ''}
          >
            Reset Notes
          </button>
          <button
            type="button"
            id="screen-confirm-deposit-btn"
            class="screen-btn screen-btn-primary"
            ${stagedBills.length === 0 || state.isBusy ? 'disabled' : ''}
          >
            Confirm Deposit (${formatCents(stagedTotal)})
          </button>
        </div>
      </div>
    `;

    const clearBtn = qs('#screen-clear-deposit-btn', this._viewportEl);
    if (clearBtn) {
      clearBtn.addEventListener('click', () => this._actions.clearDepositBills());
    }

    const confirmBtn = qs('#screen-confirm-deposit-btn', this._viewportEl);
    if (confirmBtn) {
      confirmBtn.addEventListener('click', () => {
        this._actions.submitDeposit(state.selectedAccountType, stagedBills);
      });
    }
  }

  _renderTransfer(state) {
    const fromType = state.selectedAccountType;
    const defaultTo = fromType === 'Checking' ? 'Savings' : 'Checking';
    const activeAcc = state.accounts.find((a) => a.type === fromType) || state.accounts[0];

    store.setSideKeys({
      F1: {
        label: `Transfer $50 to ${defaultTo}`,
        action: () => this._actions.transfer(fromType, defaultTo, 5000)
      },
      F2: {
        label: `Transfer $100 to ${defaultTo}`,
        action: () => this._actions.transfer(fromType, defaultTo, 10000)
      },
      F3: {
        label: `Transfer $200 to ${defaultTo}`,
        action: () => this._actions.transfer(fromType, defaultTo, 20000)
      },
      F6: {
        label: `Source: ${defaultTo}`,
        action: () => store.setState({ selectedAccountType: defaultTo, alert: null })
      },
      F7: {
        label: 'Main Menu',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null })
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div>
          <h2 class="screen-title">FUNDS TRANSFER // FROM ${escapeHtml(fromType)}</h2>
          <p class="screen-subtitle">
            Source Balance: <strong>${escapeHtml(formatCents(activeAcc?.balance || 0))}</strong>
          </p>
        </div>

        <form id="transfer-form" class="screen-panel" style="gap:10px;">
          <div class="screen-form-row">
            <label for="transfer-dest-select" class="screen-label">DESTINATION ACCOUNT OR CARD:</label>
            <select id="transfer-dest-select" class="screen-select">
              <option value="${escapeHtml(defaultTo)}">Own ${escapeHtml(defaultTo)} Account</option>
              <option value="4111-8888-9999-0000">External Card (4111-8888-9999-0000)</option>
            </select>
          </div>

          <div class="screen-form-row">
            <label for="transfer-amount-input" class="screen-label">AMOUNT IN USD ($):</label>
            <div style="display:flex; gap:8px;">
              <input
                type="number"
                id="transfer-amount-input"
                class="screen-input"
                min="1"
                step="0.01"
                placeholder="e.g. 200.00"
                required
              >
              <button type="submit" class="screen-btn screen-btn-primary" ${state.isBusy ? 'disabled' : ''}>
                Send Transfer
              </button>
            </div>
          </div>
        </form>
      </div>
    `;

    const form = qs('#transfer-form', this._viewportEl);
    const destSelect = qs('#transfer-dest-select', this._viewportEl);
    const amountInput = qs('#transfer-amount-input', this._viewportEl);
    if (form && destSelect && amountInput) {
      form.addEventListener('submit', (e) => {
        e.preventDefault();
        const dollars = Number(amountInput.value);
        const target = destSelect.value;
        wipeInput(amountInput);
        if (!Number.isFinite(dollars) || dollars <= 0) {
          store.setState({
            alert: { type: 'error', message: 'Please enter a valid positive transfer amount.' }
          });
          return;
        }
        this._actions.transfer(fromType, target, Math.round(dollars * 100));
      });
    }
  }

  _renderDispensing(state) {
    store.setSideKeys({
      F5: {
        label: 'Take Cash',
        variant: 'primary',
        action: () => this._actions.collectCash()
      },
      F6: {
        label: 'Print Receipt',
        action: () => this._actions.printPendingReceipt()
      }
    });

    const billsSummary = state.dispensedBills
      .map((item) => `${item.count} x ${formatDenomLabel(item.denomination)}`)
      .join(', ');

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="idle-hero">
          <div class="idle-pulse-badge">SHUTTER OPEN // COLLECT BANKNOTES</div>
          <h2 class="screen-title">PLEASE TAKE YOUR CASH</h2>
          <p class="screen-subtitle">
            Dispensed in lower tray: <strong>${escapeHtml(billsSummary)}</strong>
          </p>
          <div style="display:flex; gap:10px; margin-top:6px;">
            <button type="button" id="screen-take-cash-btn" class="screen-btn screen-btn-primary">
              Take Cash from Dispenser
            </button>
            <button type="button" id="screen-print-receipt-btn" class="screen-btn">
              Print Thermal Receipt
            </button>
          </div>
        </div>
      </div>
    `;

    qs('#screen-take-cash-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.collectCash();
    });
    qs('#screen-print-receipt-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.printPendingReceipt();
    });
  }

  _renderComplete(state) {
    store.setSideKeys({
      F6: {
        label: 'Print Receipt',
        variant: 'primary',
        action: () => this._actions.printPendingReceipt()
      },
      F7: {
        label: 'Main Menu',
        action: () => store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null })
      },
      F8: {
        label: 'End & Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="idle-hero">
          <div class="idle-pulse-badge">TRANSACTION COMMITTED</div>
          <h2 class="screen-title">OPERATION SUCCESSFUL</h2>
          <p class="screen-subtitle">
            Would you like to perform another transaction or terminate the session and eject your card?
          </p>
          <div style="display:flex; gap:8px; flex-wrap:wrap; justify-content:center;">
            <button type="button" id="comp-receipt-btn" class="screen-btn">
              Print Electronic Receipt
            </button>
            <button type="button" id="comp-menu-btn" class="screen-btn screen-btn-primary">
              Return to Main Menu
            </button>
            <button type="button" id="comp-eject-btn" class="screen-btn screen-btn-danger">
              End Session &amp; Eject Card
            </button>
          </div>
        </div>
      </div>
    `;

    qs('#comp-receipt-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.printPendingReceipt();
    });
    qs('#comp-menu-btn', this._viewportEl)?.addEventListener('click', () => {
      store.setState({ customerView: CUSTOMER_VIEWS.MENU, alert: null });
    });
    qs('#comp-eject-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.ejectCard();
    });
  }
}
