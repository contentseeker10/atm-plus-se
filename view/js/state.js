export const FSM_STATES = Object.freeze({
  IDLE: 'Idle',
  CARD_INSERTED: 'CardInserted',
  CUSTOMER_SESSION: 'CustomerSession',
  ENGINEER_SESSION: 'EngineerSession'
});

export const CUSTOMER_VIEWS = Object.freeze({
  MENU: 'MENU',
  BALANCE: 'BALANCE',
  WITHDRAW: 'WITHDRAW',
  DEPOSIT: 'DEPOSIT',
  TRANSFER: 'TRANSFER',
  DISPENSING: 'DISPENSING',
  COMPLETE: 'COMPLETE'
});

export const ENGINEER_VIEWS = Object.freeze({
  AUTH_KEY: 'AUTH_KEY',
  VAULT: 'VAULT',
  REPLENISH: 'REPLENISH',
  AUDIT: 'AUDIT'
});

const initialState = {
  fsmState: FSM_STATES.IDLE,
  customerView: CUSTOMER_VIEWS.MENU,
  engineerView: ENGINEER_VIEWS.AUTH_KEY,
  cardNumber: null,
  maskedPan: null,
  cardHolder: null,
  role: 'None',
  token: null,
  masterKey: null,
  remainingAttempts: 3,
  pinLength: 0,
  isBusy: false,
  busyMessage: '',
  alert: null,
  accounts: [],
  selectedAccountType: 'Checking',
  dispensedBills: [],
  lastReceipt: null,
  vaultStatus: null,
  auditRecords: [],
  integrityStatus: null,
  timeoutRemainingSec: 0,
  sideKeys: {
    F1: null, F2: null, F3: null, F4: null,
    F5: null, F6: null, F7: null, F8: null
  }
};

class Store {
  constructor() {
    this._state = structuredClone(initialState);
    this._listeners = new Set();
  }

  getState() {
    return this._state;
  }

  setState(patch) {
    const nextPatch = typeof patch === 'function' ? patch(this._state) : patch;
    this._state = { ...this._state, ...nextPatch };
    this._notify();
  }

  setSideKeys(bindings) {
    this._state.sideKeys = {
      F1: null, F2: null, F3: null, F4: null,
      F5: null, F6: null, F7: null, F8: null,
      ...bindings
    };
    this._notifySideKeys();
  }

   subscribe(listener) {
    this._listeners.add(listener);
    return () => this._listeners.delete(listener);
  }

  onSideKeysChange(listener) {
    this._sideKeyListener = listener;
  }

  resetSession(alert = null) {
    this._state = {
      ...structuredClone(initialState),
      alert
    };
    this._notify();
    this._notifySideKeys();
  }

  _notify() {
    for (const listener of this._listeners) {
      listener(this._state);
    }
  }

  _notifySideKeys() {
    if (this._sideKeyListener) {
      this._sideKeyListener(this._state.sideKeys);
    }
  }
}

export const store = new Store();
