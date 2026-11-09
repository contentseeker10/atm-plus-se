import { CONFIG } from '../config.js';

export class InactivityTimer {
  constructor({ onTick, onExpire }) {
    this._onTick = onTick;
    this._onExpire = onExpire;
    this._intervalId = null;
    this._remainingSec = 0;
    this._active = false;
    this._boundReset = () => {
      if (this._active) {
        this.reset();
      }
    };
  }

  start(durationSec = CONFIG.INACTIVITY_TIMEOUT_SEC) {
    this.stop();
    this._active = true;
    this._remainingSec = durationSec;
    this._emitTick();
    this._attachListeners();

    this._intervalId = window.setInterval(() => {
      if (!this._active) return;
      this._remainingSec -= 1;
      this._emitTick();

      if (this._remainingSec <= 0) {
        this.stop();
        if (typeof this._onExpire === 'function') {
          this._onExpire();
        }
      }
    }, 1000);
  }

  reset(durationSec = CONFIG.INACTIVITY_TIMEOUT_SEC) {
    if (!this._active) return;
    this._remainingSec = durationSec;
    this._emitTick();
  }

  stop() {
    this._active = false;
    if (this._intervalId !== null) {
      window.clearInterval(this._intervalId);
      this._intervalId = null;
    }
    this._remainingSec = 0;
    this._emitTick();
    this._detachListeners();
  }

  _emitTick() {
    if (typeof this._onTick === 'function') {
      this._onTick(this._remainingSec, this._active);
    }
  }

  _attachListeners() {
    window.addEventListener('pointerdown', this._boundReset, { passive: true });
    window.addEventListener('keydown', this._boundReset, { passive: true });
  }

  _detachListeners() {
    window.removeEventListener('pointerdown', this._boundReset);
    window.removeEventListener('keydown', this._boundReset);
  }
}
