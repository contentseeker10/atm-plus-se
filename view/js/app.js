import { CONFIG, updateConfig } from './config.js';
import {
  CUSTOMER_VIEWS,
  ENGINEER_VIEWS,
  FSM_STATES,
  store
} from './state.js';
import { api } from './api.js';
import { CardSlotComponent } from './components/card-slot.js';
import { PinPadComponent } from './components/pin-pad.js';
import { BillAcceptorComponent } from './components/bill-acceptor.js';
import { ReceiptComponent } from './components/receipt.js';
import { CustomerUiComponent } from './components/customer-ui.js';
import { EngineerUiComponent } from './components/engineer-ui.js';
import { InactivityTimer } from './utils/timer.js';
import {
  escapeHtml,
  formatCents,
  maskCardNumber,
  qs,
  qsa,
  setText
} from './utils/dom.js';

class AtmSimulatorApp {
  constructor() {
    this._cacheElements();
    this._initComponents();
    this._bindBezelKeys();
    this._bindEnvironmentBar();

    store.subscribe((state) => this._renderAll(state));
    store.onSideKeysChange((sideKeys) => this._renderSideKeys(sideKeys));

    this._syncEnvironmentBar();
    this._renderAll(store.getState());
  }

  _cacheElements() {
    this._screenEl = qs('#atm-screen');
    this._viewportEl = qs('#screen-viewport');
    this._sideLeftEl = qs('#screen-side-left');
    this._sideRightEl = qs('#screen-side-right');
    this._alertEl = qs('#screen-alert');
    this._loadingEl = qs('#screen-loading');
    this._loadingTextEl = qs('#screen-loading-text');
    this._cardBannerEl = qs('#screen-card-banner');
    this._sessionTimerLabelEl = qs('#screen-session-timer');

    this._fsmBadgeEl = qs('#fsm-state-badge');
    this._roleBadgeEl = qs('#session-role-badge');
    this._timeoutPillEl = qs('#timeout-pill');
    this._timeoutCounterEl = qs('#timeout-counter');

    this._apiUrlInput = qs('#api-base-url-input');
    this._applyUrlBtn = qs('#apply-api-url-btn');
  }

  _initComponents() {
    this._timer = new InactivityTimer({
      onTick: (remainingSec, active) => {
        if (!active) {
          this._timeoutPillEl?.classList.add('hidden');
          setText(this._sessionTimerLabelEl, '');
          return;
        }
        this._timeoutPillEl?.classList.remove('hidden');
        setText(this._timeoutCounterEl, `${remainingSec}s`);
        setText(this._sessionTimerLabelEl, `AUTO-EJECT IN ${remainingSec}S`);
      },
      onExpire: () => {
        this.handleEjectCard({
          type: 'warn',
          message: 'Session timed out due to inactivity. Card automatically ejected.'
        });
      }
    });

    this._cardSlot = new CardSlotComponent({
      slotRoot: qs('#card-slot-root'),
      walletRoot: qs('#test-cards-root'),
      ledLabelEl: qs('#card-led-label'),
      onInsertCard: (pan) => this.handleInsertCard(pan),
      onEjectCard: () => this.handleEjectCard()
    });

    this._pinPad = new PinPadComponent({
      rootEl: qs('#pin-pad-root'),
      onSubmitPin: (pin) => this.handleVerifyPin(pin),
      onCancel: () => this.handleEjectCard(),
      onNumericInput: (key) => this._forwardNumericKeyToInput(key)
    });

    this._billAcceptor = new BillAcceptorComponent({
      rootEl: qs('#bill-acceptor-root'),
      shutterLabelEl: qs('#shutter-state-label'),
      onDepositConfirm: (bills) =>
        this.handleDeposit(store.getState().selectedAccountType, bills),
      onCollectDispensed: () => this.handleCollectDispensedCash(),
      onStagedChange: () => {
        if (store.getState().customerView === CUSTOMER_VIEWS.DEPOSIT) {
          this._customerUi.render(store.getState());
        }
      }
    });

    this._receipt = new ReceiptComponent({
      rootEl: qs('#receipt-slot-root'),
      onDismiss: () => store.setState({ lastReceipt: null })
    });

    this._customerUi = new CustomerUiComponent({
      viewportEl: this._viewportEl,
      actions: {
        ejectCard: () => this.handleEjectCard(),
        withdraw: (accountType, amountCents) =>
          this.handleWithdraw(accountType, amountCents),
        getStagedBills: () => this._billAcceptor.getStagedBills(),
        addDepositBill: (denomCents) => {
          this._billAcceptor.addStagedBill(denomCents);
          this._billAcceptor.render(store.getState());
        },
        clearDepositBills: () => {
          this._billAcceptor.clearStagedBills();
          this._billAcceptor.render(store.getState());
        },
        submitDeposit: (accountType, bills) =>
          this.handleDeposit(accountType, bills),
        transfer: (from, to, amountCents) =>
          this.handleTransfer(from, to, amountCents),
        openAutoPay: () => this.handleOpenAutoPay(),
        createAutoPay: (payload) => this.handleCreateAutoPay(payload),
        runAutoPay: () => this.handleRunAutoPay(),
        deleteAutoPay: (ruleId) => this.handleDeleteAutoPay(ruleId),
        collectCash: () => this.handleCollectDispensedCash(),
        printPendingReceipt: () => this.handlePrintPendingReceipt()
      }
    });

    this._engineerUi = new EngineerUiComponent({
      viewportEl: this._viewportEl,
      actions: {
        ejectCard: () => this.handleEjectCard(),
        authenticateMasterKey: (masterKey) =>
          this.handleEngineerMasterKey(masterKey),
        refreshVault: () => this.handleRefreshVault(),
        toggleVaultDoor: (open) => this.handleToggleVaultDoor(open),
        replenishCassette: (cassetteIndex, addCount) =>
          this.handleReplenishCassette(cassetteIndex, addCount),
        replaceCassette: (cassetteIndex, setCount) =>
          this.handleReplaceCassette(cassetteIndex, setCount),
        unlockCard: (cardNumber) => this.handleUnlockCard(cardNumber),
        simulateTamper: (restore) => this.handleSimulateTamper(restore),
        resetHardwareCounters: () => this.handleResetHardwareCounters(),
        openAuditView: (customAlert = null) => this.handleOpenAuditView(customAlert),
        verifyAuditIntegrity: () => this.handleVerifyIntegrity()
      }
    });
  }

  _bindBezelKeys() {
    qsa('.bezel-key').forEach((btn) => {
      btn.addEventListener('click', () => {
        const fkey = btn.getAttribute('data-fkey');
        const binding = store.getState().sideKeys[fkey];
        if (binding && typeof binding.action === 'function' && !store.getState().isBusy) {
          binding.action();
        }
      });
    });
  }

  _bindEnvironmentBar() {
    this._applyUrlBtn?.addEventListener('click', () => {
      const nextUrl = this._apiUrlInput?.value || '';
      updateConfig({ apiBaseUrl: nextUrl });
      this._syncEnvironmentBar();
      store.setState({
        alert: { type: 'info', message: `API Base URL updated to ${CONFIG.API_BASE_URL}` }
      });
    });
  }

  _syncEnvironmentBar() {
    if (this._apiUrlInput) {
      this._apiUrlInput.value = CONFIG.API_BASE_URL;
    }
  }

  _forwardNumericKeyToInput(key) {
    const numInput = qs('input[type="number"]', this._viewportEl);
    if (!numInput) return;
    if (key === 'CLEAR') {
      numInput.value = String(numInput.value).slice(0, -1);
      return;
    }
    if (/^[0-9]+$/.test(key)) {
      numInput.value = `${numInput.value}${key}`;
    }
  }

  _updateWalletCardOverride(cardNumber, patch) {
    if (!cardNumber) return;
    const current = store.getState().cardStatusOverrides || {};
    const nextOverrides = {
      ...current,
      [cardNumber]: {
        ...(current[cardNumber] || {}),
        ...patch
      }
    };
    store.setState({ cardStatusOverrides: nextOverrides });
  }

  _syncWalletAccountsHint(cardNumber, accounts) {
    if (!cardNumber || !Array.isArray(accounts) || accounts.length === 0) return;
    const checking = accounts.find((a) => a.type === 'Checking');
    const savings = accounts.find((a) => a.type === 'Savings');
    const parts = [];
    if (checking) parts.push(`${checking.id || 'CHK'} (${formatCents(checking.balance)})`);
    if (savings) parts.push(`${savings.id || 'SAV'} (${formatCents(savings.balance)})`);
    if (parts.length > 0) {
      this._updateWalletCardOverride(cardNumber, {
        accountsHint: parts.join(' / '),
        blocked: false
      });
    }
  }

  async _withBusy(busyMessage, fn) {
    store.setState({ isBusy: true, busyMessage, alert: null });
    try {
      return await fn();
    } finally {
      if (store.getState().isBusy) {
        store.setState({ isBusy: false, busyMessage: '' });
      }
    }
  }

  async handleInsertCard(cardNumber) {
    this._pinPad.clearBuffer();
    this._billAcceptor.clearStagedBills();

    try {
      const res = await this._withBusy('READING SMART CARD CHIP...', () =>
        api.insertCard(cardNumber)
      );

      this._cardSlot.setLedState('reading');
      this._timer.start();

      store.setState({
        fsmState: FSM_STATES.CARD_INSERTED,
        cardNumber,
        maskedPan: res.maskedPan || maskCardNumber(cardNumber),
        cardHolder: res.cardHolder || 'CARDHOLDER',
        remainingAttempts: res.remainingAttempts ?? 3,
        pinLength: 0,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'info',
          message: 'Card validated. Enter your 4-digit PIN code.'
        }
      });
    } catch (err) {
      this._cardSlot.setLedState('error');
      if (err.errorCode === 'CardBlocked' || err.errorCode === 'MaxAttemptsExceeded') {
        this._updateWalletCardOverride(cardNumber, { blocked: true });
      }
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'AuthError'}] ${err.message}`
        }
      });
    }
  }

  async handleVerifyPin(pin) {
    const { cardNumber } = store.getState();
    if (!cardNumber) return;

    try {
      const res = await this._withBusy('VERIFYING PIN WITH HSM...', () =>
        api.verifyPin(cardNumber, pin)
      );

      this._timer.reset();

      if (res.role === 'Engineer') {
        store.setState({
          fsmState: FSM_STATES.ENGINEER_SESSION,
          engineerView: ENGINEER_VIEWS.AUTH_KEY,
          role: 'Engineer',
          token: res.token,
          cardHolder: res.cardHolder || store.getState().cardHolder,
          remainingAttempts: 3,
          isBusy: false,
          busyMessage: '',
          alert: {
            type: 'info',
            message: 'Engineer PIN verified. Enter Vault Master Key to proceed.'
          }
        });
        return;
      }

      const accData = await this._withBusy('LOADING CUSTOMER ACCOUNTS...', () =>
        api.getCustomerAccounts(res.token)
      );

      this._syncWalletAccountsHint(cardNumber, accData.accounts || []);

      store.setState({
        fsmState: FSM_STATES.CUSTOMER_SESSION,
        customerView: CUSTOMER_VIEWS.MENU,
        role: 'Customer',
        token: res.token,
        cardHolder: res.cardHolder || store.getState().cardHolder,
        accounts: accData.accounts || [],
        selectedAccountType: accData.accounts?.[0]?.type || 'Checking',
        remainingAttempts: 3,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: 'Authentication successful. Select a banking service.'
        }
      });
    } catch (err) {
      const remaining = err.payload?.remainingAttempts;
      const cardBlocked = err.payload?.cardBlocked || err.errorCode === 'MaxAttemptsExceeded';

      if (cardBlocked) {
        this._updateWalletCardOverride(cardNumber, { blocked: true });
        this._timer.stop();
        this._cardSlot.triggerEjectAnimation();
        store.resetSession({
          type: 'error',
          message: `[${err.errorCode}] ${err.message}`
        });
        return;
      }

      store.setState({
        remainingAttempts:
          typeof remaining === 'number' ? remaining : store.getState().remainingAttempts,
        pinLength: 0,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'InvalidPin'}] ${err.message}`
        }
      });
    }
  }

  async handleEjectCard(customAlert = null) {
    this._timer.stop();
    this._pinPad.clearBuffer();
    this._billAcceptor.clearStagedBills();
    this._pendingReceipt = null;

    const hadCard = store.getState().fsmState !== FSM_STATES.IDLE;
    if (hadCard) {
      try {
        await api.ejectCard();
      } catch (_) {}
      this._cardSlot.triggerEjectAnimation();
    }

    store.resetSession(
      customAlert ||
        (hadCard
          ? { type: 'info', message: 'Session ended. Please take your card from the slot.' }
          : null)
    );
  }

  async handleWithdraw(accountType, amountCents) {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('VERIFYING LEDGER & DISPENSING BANKNOTES...', () =>
        api.withdraw(token, accountType, amountCents, store.nextSessionNonce())
      );

      this._pendingReceipt = res.receipt || null;
      this._syncWalletAccountsHint(store.getState().cardNumber, res.accounts || []);
      const overdraftNote =
        res.overdraftCoveredCents > 0
          ? ` (Includes $${(res.overdraftCoveredCents / 100).toFixed(2)} overdraft protection from Savings)`
          : '';

      store.setState({
        accounts: res.accounts || store.getState().accounts,
        dispensedBills: res.dispensedBills || [],
        customerView: CUSTOMER_VIEWS.DISPENSING,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Dispensed $${(amountCents / 100).toFixed(2)}${overdraftNote}. Please collect your cash.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'WithdrawError'}] ${err.message}`
        }
      });
    }
  }

  handleCollectDispensedCash() {
    store.setState({
      dispensedBills: [],
      customerView: CUSTOMER_VIEWS.COMPLETE,
      alert: {
        type: 'success',
        message: 'Cash collected from dispenser tray. Shutter closed.'
      }
    });
  }

  handlePrintPendingReceipt() {
    if (!this._pendingReceipt) {
      store.setState({
        alert: { type: 'info', message: 'Receipt already printed below.' }
      });
      return;
    }
    store.setState({
      lastReceipt: this._pendingReceipt,
      alert: { type: 'success', message: 'Electronic receipt printed to thermal slot.' }
    });
  }

  async handleDeposit(accountType, bills) {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('VALIDATING BANKNOTES & CREDITING LEDGER...', () =>
        api.deposit(token, accountType, bills, store.nextSessionNonce())
      );

      this._billAcceptor.clearStagedBills();
      this._pendingReceipt = res.receipt || null;
      this._syncWalletAccountsHint(store.getState().cardNumber, res.accounts || []);

      const sweepNote =
        res.overflowSweptCents > 0
          ? ` ($${(res.overflowSweptCents / 100).toFixed(2)} swept to Savings via Overflow Protection)`
          : '';

      store.setState({
        accounts: res.accounts || store.getState().accounts,
        lastReceipt: res.receipt || null,
        customerView: CUSTOMER_VIEWS.COMPLETE,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Deposited $${(res.totalDeposited / 100).toFixed(2)} to ${accountType}${sweepNote}.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'DepositError'}] ${err.message}`
        }
      });
    }
  }

  async handleTransfer(from, to, amountCents) {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('EXECUTING ANTI-REPLAY LEDGER TRANSFER...', () =>
        api.transfer(token, from, to, amountCents, store.nextSessionNonce())
      );

      this._pendingReceipt = res.receipt || null;
      this._syncWalletAccountsHint(store.getState().cardNumber, res.accounts || []);

      const protectionNotes = [];
      if (res.overdraftCoveredCents > 0) {
        protectionNotes.push(
          `Includes $${(res.overdraftCoveredCents / 100).toFixed(2)} overdraft protection from Savings`
        );
      }
      if (res.overflowSweptCents > 0) {
        protectionNotes.push(
          `$${(res.overflowSweptCents / 100).toFixed(2)} swept to Savings via Overflow Protection`
        );
      }
      const noteSuffix =
        protectionNotes.length > 0 ? ` (${protectionNotes.join('; ')})` : '';

      store.setState({
        accounts: res.accounts || store.getState().accounts,
        lastReceipt: res.receipt || null,
        customerView: CUSTOMER_VIEWS.COMPLETE,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Transferred $${(amountCents / 100).toFixed(2)} from ${from} to ${to}${noteSuffix}.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'TransferError'}] ${err.message}`
        }
      });
    }
  }

  async handleOpenAutoPay() {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('LOADING AUTO-PAYMENT RULES...', () =>
        api.getAutoPayments(token)
      );
      store.setState({
        apsRules: res.rules || [],
        customerView: CUSTOMER_VIEWS.AUTOPAY,
        isBusy: false,
        busyMessage: '',
        alert: null
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'AutoPayError'}] ${err.message}`
        }
      });
    }
  }

  async handleCreateAutoPay(payload) {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('REGISTERING AUTO-PAYMENT RULE...', () =>
        api.createAutoPayment(token, payload)
      );
      store.setState({
        apsRules: res.rules || [],
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Auto-payment rule ${res.rule?.ruleId || ''} created.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'AutoPayError'}] ${err.message}`
        }
      });
    }
  }

  async handleRunAutoPay() {
    const { token } = store.getState();
    if (!token) return;

    try {
      const res = await this._withBusy('EXECUTING DUE AUTO-PAYMENTS...', () =>
        api.runAutoPayments(token)
      );
      const executed = res.executed ?? 0;
      const failureCount = Array.isArray(res.failures)
        ? res.failures.length
        : Number(res.failures) || 0;
      this._syncWalletAccountsHint(store.getState().cardNumber, res.accounts || []);
      store.setState({
        accounts: res.accounts || store.getState().accounts,
        apsRules: res.rules || store.getState().apsRules,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: failureCount > 0 ? 'warn' : 'success',
          message: `Auto-payment run completed: ${executed} executed, ${failureCount} failed.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'AutoPayError'}] ${err.message}`
        }
      });
    }
  }

  async handleDeleteAutoPay(ruleId) {
    const { token } = store.getState();
    if (!token || !ruleId) return;

    try {
      const res = await this._withBusy('REMOVING AUTO-PAYMENT RULE...', () =>
        api.deleteAutoPayment(token, ruleId)
      );
      store.setState({
        apsRules: res.rules || [],
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'info',
          message: `Auto-payment rule ${ruleId} removed.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'AutoPayError'}] ${err.message}`
        }
      });
    }
  }

  async handleEngineerMasterKey(masterKey) {
    const { token } = store.getState();
    if (!token) return;

    try {
      const vaultStatus = await this._withBusy('VERIFYING VAULT MASTER KEY...', () =>
        api.getVaultDiagnostics(token, masterKey)
      );

      store.setState({
        masterKey,
        vaultStatus,
        engineerView: ENGINEER_VIEWS.VAULT,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: 'Master Key verified. Vault diagnostics unlocked.'
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'error',
          message: `[${err.errorCode || 'VaultAccessDenied'}] ${err.message}`
        }
      });
    }
  }

  async handleRefreshVault() {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      const vaultStatus = await this._withBusy('POLLING CASSETTE SENSORS...', () =>
        api.getVaultDiagnostics(token, masterKey)
      );
      store.setState({
        vaultStatus,
        engineerView: ENGINEER_VIEWS.VAULT,
        isBusy: false,
        busyMessage: ''
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: err.message }
      });
    }
  }

  async handleToggleVaultDoor(open) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      await this._withBusy(open ? 'UNLOCKING SAFE BOLTS...' : 'ENGAGING SAFE INTERLOCK...', () =>
        api.setVaultDoor(token, masterKey, open)
      );
      const vaultStatus = await api.getVaultDiagnostics(token, masterKey);
      store.setState({
        vaultStatus,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: open ? 'warn' : 'success',
          message: open
            ? 'Safe door is now OPEN. Cassettes ready for replenishment.'
            : 'Safe door is now CLOSED and locked.'
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: err.message }
      });
    }
  }

  async handleReplenishCassette(cassetteIndex, addCount) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      await this._withBusy(`LOADING ${addCount} NOTES INTO CASSETTE #${cassetteIndex + 1}...`, () =>
        api.replenishCassette(token, masterKey, { cassetteIndex, addCount })
      );
      const vaultStatus = await api.getVaultDiagnostics(token, masterKey);
      store.setState({
        vaultStatus,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Added ${addCount} banknotes to Cassette #${cassetteIndex + 1}.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: `[${err.errorCode}] ${err.message}` }
      });
    }
  }

  async handleReplaceCassette(cassetteIndex, setCount) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      await this._withBusy(
        `SETTING CASSETTE #${cassetteIndex + 1} TO ${setCount} NOTES...`,
        () => api.replenishCassette(token, masterKey, { cassetteIndex, setCount })
      );
      const vaultStatus = await api.getVaultDiagnostics(token, masterKey);
      store.setState({
        vaultStatus,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Cassette #${cassetteIndex + 1} set to ${setCount} banknotes.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: `[${err.errorCode || 'ReplenishError'}] ${err.message}` }
      });
    }
  }

  async handleUnlockCard(cardNumber) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey || !cardNumber) return;

    try {
      await this._withBusy(`UNLOCKING CARD ${maskCardNumber(cardNumber)}...`, () =>
        api.unlockCard(token, masterKey, cardNumber)
      );
      this._updateWalletCardOverride(cardNumber, { blocked: false });
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: `Card ${cardNumber} unlocked and PIN retry counter reset.`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: `[${err.errorCode || 'CardUnlockError'}] ${err.message}` }
      });
    }
  }

  async handleResetHardwareCounters() {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      const vaultStatus = await this._withBusy('RESETTING HARDWARE COUNTERS...', () =>
        api.replenishCassette(token, masterKey, { resetCounters: true })
      );
      store.setState({
        vaultStatus,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: 'success',
          message: 'Electromechanical cycle counters reset to zero.'
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: `[${err.errorCode}] ${err.message}` }
      });
    }
  }

  async handleOpenAuditView(customAlert = null) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      const res = await this._withBusy('FETCHING CRYPTOGRAPHIC AUDIT LOG...', () =>
        api.getAuditLog(token, masterKey)
      );
      store.setState({
        auditRecords: res.records || [],
        engineerView: ENGINEER_VIEWS.AUDIT,
        isBusy: false,
        busyMessage: '',
        alert: customAlert
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: err.message }
      });
    }
  }

  async handleVerifyIntegrity() {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      const [auditRes, verifyRes] = await this._withBusy(
        'RECOMPUTING SHA-256 + HMAC HASH CHAIN...',
        () => Promise.all([api.getAuditLog(token, masterKey), api.verifyIntegrity(token, masterKey)])
      );

      store.setState({
        auditRecords: auditRes.records || [],
        integrityStatus: verifyRes,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: verifyRes.valid ? 'success' : 'error',
          message: verifyRes.detail
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: err.message }
      });
    }
  }

  async handleSimulateTamper(restore = false) {
    const { token, masterKey } = store.getState();
    if (!token || !masterKey) return;

    try {
      await this._withBusy(
        restore
          ? 'RESTORING TAMPERED LEDGER ENTRY...'
          : 'SIMULATING SQLITE AUDIT TAMPER...',
        () => api.simulateTamper(token, masterKey, restore)
      );
      const [auditRes, verifyRes] = await Promise.all([
        api.getAuditLog(token, masterKey),
        api.verifyIntegrity(token, masterKey)
      ]);
      store.setState({
        auditRecords: auditRes.records || [],
        integrityStatus: verifyRes,
        isBusy: false,
        busyMessage: '',
        alert: {
          type: verifyRes.valid ? 'success' : 'error',
          message: restore
            ? `Tampered record restored. ${verifyRes.detail}`
            : `SQLite audit record altered externally! ${verifyRes.detail}`
        }
      });
    } catch (err) {
      store.setState({
        isBusy: false,
        busyMessage: '',
        alert: { type: 'error', message: `[${err.errorCode || 'TamperSimError'}] ${err.message}` }
      });
    }
  }

  _renderSideKeys(sideKeys) {
    const leftKeys = ['F1', 'F2', 'F3', 'F4'];
    const rightKeys = ['F5', 'F6', 'F7', 'F8'];

    const buildSlot = (fkey) => {
      const cfg = sideKeys[fkey];
      const enabled = Boolean(cfg && typeof cfg.action === 'function');
      const variantCls =
        cfg?.variant === 'danger'
          ? 'softkey-danger'
          : cfg?.variant === 'primary'
            ? 'softkey-primary'
            : '';
      return `
        <button
          type="button"
          class="softkey-slot ${enabled ? 'softkey-enabled' : ''} ${variantCls}"
          data-softkey="${fkey}"
          ${!enabled ? 'disabled' : ''}
        >
          <span>${escapeHtml(cfg?.label || '---')}</span>
        </button>
      `;
    };

    if (this._sideLeftEl) {
      this._sideLeftEl.innerHTML = leftKeys.map(buildSlot).join('');
    }
    if (this._sideRightEl) {
      this._sideRightEl.innerHTML = rightKeys.map(buildSlot).join('');
    }

    qsa('[data-softkey]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const fkey = btn.getAttribute('data-softkey');
        const cfg = store.getState().sideKeys[fkey];
        if (cfg && typeof cfg.action === 'function' && !store.getState().isBusy) {
          cfg.action();
        }
      });
    });

    qsa('.bezel-key').forEach((btn) => {
      const fkey = btn.getAttribute('data-fkey');
      const enabled = Boolean(sideKeys[fkey]?.action);
      btn.classList.toggle('key-active', enabled);
      btn.disabled = !enabled;
    });
  }

  _renderAll(state) {
    const isEngineer = state.fsmState === FSM_STATES.ENGINEER_SESSION;
    this._screenEl?.classList.toggle('mode-engineer', isEngineer);
    this._screenEl?.classList.toggle('mode-customer', !isEngineer);

    if (this._fsmBadgeEl) {
      this._fsmBadgeEl.textContent = state.fsmState;
      this._fsmBadgeEl.className = `telemetry-value ${
        state.fsmState === FSM_STATES.IDLE
          ? 'state-idle'
          : isEngineer
            ? 'state-engineer'
            : 'state-active'
      }`;
    }

    setText(this._roleBadgeEl, state.role || 'None');
    setText(
      this._cardBannerEl,
      state.cardNumber
        ? `${state.maskedPan || maskCardNumber(state.cardNumber)} // ${state.cardHolder || ''}`
        : 'NO CARD INSERTED'
    );

    if (this._loadingEl) {
      this._loadingEl.classList.toggle('hidden', !state.isBusy);
      setText(this._loadingTextEl, state.busyMessage || 'PROCESSING COMMAND...');
    }

    if (this._alertEl) {
      if (!state.alert) {
        this._alertEl.className = 'screen-alert hidden';
        this._alertEl.textContent = '';
      } else {
        this._alertEl.className = `screen-alert alert-${state.alert.type || 'info'}`;
        this._alertEl.textContent = state.alert.message;
      }
    }

    if (isEngineer) {
      this._engineerUi.render(state);
    } else {
      this._customerUi.render(state);
    }

    this._cardSlot.render(state);
    this._billAcceptor.render(state);
    this._receipt.render(state);
  }
}

window.addEventListener('DOMContentLoaded', () => {
  new AtmSimulatorApp();
});
