import { CONFIG } from '../config.js';
import { ENGINEER_VIEWS, store } from '../state.js';
import { toggleMockAuditTamper } from '../api.js';
import {
  escapeHtml,
  formatCents,
  formatDenomLabel,
  qs,
  shortHash,
  wipeInput
} from '../utils/dom.js';

export class EngineerUiComponent {
  constructor({ viewportEl, actions }) {
    this._viewportEl = viewportEl;
    this._actions = actions;
  }

  render(state) {
    if (!this._viewportEl) return;

    switch (state.engineerView) {
      case ENGINEER_VIEWS.VAULT:
        this._renderVault(state);
        break;
      case ENGINEER_VIEWS.REPLENISH:
        this._renderReplenish(state);
        break;
      case ENGINEER_VIEWS.AUDIT:
        this._renderAudit(state);
        break;
      case ENGINEER_VIEWS.AUTH_KEY:
      default:
        this._renderAuthKey(state);
        break;
    }
  }

  _renderAuthKey(state) {
    store.setSideKeys({
      F5: {
        label: 'Use Hardware Key',
        variant: 'primary',
        action: () => this._actions.authenticateMasterKey('ENG-MASTER-KEY-2026')
      },
      F8: {
        label: 'Eject Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="eng-header-bar">
          <div>
            <h2 class="screen-title">ENGINEER MAINTENANCE AUTH</h2>
            <p class="screen-subtitle">
              Role verified as <strong>Engineer</strong>. Provide Vault Master Key to unlock diagnostics.
            </p>
          </div>
          <span class="eng-badge door-open">CLEARANCE LEVEL 2</span>
        </div>

        <form id="eng-master-key-form" class="screen-panel" style="gap:10px;">
          <div class="screen-form-row">
            <label for="eng-master-key-input" class="screen-label">
              VAULT MASTER CRYPTOGRAPHIC KEY:
            </label>
            <input
              type="password"
              id="eng-master-key-input"
              class="screen-input"
              placeholder="Enter Master Key (ENG-MASTER-KEY-2026)"
              autocomplete="off"
              required
            >
          </div>
          <div style="display:flex; gap:8px; flex-wrap:wrap;">
            <button type="submit" class="screen-btn screen-btn-primary" ${state.isBusy ? 'disabled' : ''}>
              Unlock Maintenance Console
            </button>
            <button
              type="button"
              id="eng-autofill-key-btn"
              class="screen-btn"
              ${state.isBusy ? 'disabled' : ''}
            >
              Insert Token (ENG-MASTER-KEY-2026)
            </button>
          </div>
        </form>
      </div>
    `;

    const form = qs('#eng-master-key-form', this._viewportEl);
    const input = qs('#eng-master-key-input', this._viewportEl);
    if (form && input) {
      form.addEventListener('submit', (e) => {
        e.preventDefault();
        const key = input.value.trim();
        wipeInput(input);
        if (key) {
          this._actions.authenticateMasterKey(key);
        }
      });
    }

    qs('#eng-autofill-key-btn', this._viewportEl)?.addEventListener('click', () => {
      if (input) wipeInput(input);
      this._actions.authenticateMasterKey('ENG-MASTER-KEY-2026');
    });
  }

  _renderVault(state) {
    const vault = state.vaultStatus || {
      totalCash: 0,
      doorOpen: false,
      cassettes: [],
      hardwareCounters: { dispenseCycles: 0, depositCycles: 0, jamEvents: 0 }
    };

    store.setSideKeys({
      F1: {
        label: 'Vault Diagnostics',
        variant: 'primary',
        action: () => this._actions.refreshVault()
      },
      F2: {
        label: 'Replenish Cash',
        action: () => store.setState({ engineerView: ENGINEER_VIEWS.REPLENISH, alert: null })
      },
      F3: {
        label: 'Audit & Integrity',
        action: () => this._actions.openAuditView()
      },
      F5: {
        label: vault.doorOpen ? 'Lock Safe Door' : 'Open Safe Door',
        action: () => this._actions.toggleVaultDoor(!vault.doorOpen)
      },
      F6: {
        label: 'Reset Counters',
        action: () => this._actions.resetHardwareCounters()
      },
      F8: {
        label: 'Eject Service Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const totalNotes = vault.cassettes.reduce((sum, c) => sum + c.count, 0);
    const counters = vault.hardwareCounters || { dispenseCycles: 0, depositCycles: 0, jamEvents: 0 };

    const cassettesHtml = vault.cassettes
      .map((c, idx) => {
        const pct = Math.min(100, Math.round((c.count / c.capacity) * 100));
        const levelClass = pct < 15 ? 'level-low' : pct < 35 ? 'level-warn' : '';
        return `
          <div class="cassette-card">
            <div class="cassette-top">
              <span class="cassette-denom">CASSETTE #${idx + 1} // ${escapeHtml(formatDenomLabel(c.denom))}</span>
              <span class="cassette-pct">${pct}%</span>
            </div>
            <div class="cassette-meter">
              <div class="cassette-meter-fill ${levelClass}" style="width:${pct}%"></div>
            </div>
            <div class="cassette-meta">
              <span>${escapeHtml(c.count)} / ${escapeHtml(c.capacity)} notes</span>
              <span>${escapeHtml(formatCents(c.denom * c.count))}</span>
            </div>
          </div>
        `;
      })
      .join('');

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="eng-header-bar">
          <div>
            <h2 class="screen-title">VAULT DIAGNOSTICS &amp; SENSORS</h2>
            <p class="screen-subtitle">
              Counters: Dispenses=${escapeHtml(counters.dispenseCycles)} | Deposits=${escapeHtml(counters.depositCycles)} | Jams=${escapeHtml(counters.jamEvents)}
            </p>
          </div>
          <span class="eng-badge ${vault.doorOpen ? 'door-open' : 'door-closed'}">
            SAFE DOOR SENSOR: ${vault.doorOpen ? 'OPEN' : 'CLOSED'}
          </span>
        </div>

        <div class="vault-summary-strip">
          <div class="vault-stat-box">
            <span class="vault-stat-label">TOTAL VAULT CASH</span>
            <span class="vault-stat-value">${escapeHtml(formatCents(vault.totalCash))}</span>
          </div>
          <div class="vault-stat-box">
            <span class="vault-stat-label">BANKNOTE COUNT</span>
            <span class="vault-stat-value">${escapeHtml(totalNotes)} NOTES</span>
          </div>
          <div class="vault-stat-box">
            <span class="vault-stat-label">SAFE INTERLOCK</span>
            <span class="vault-stat-value">${vault.doorOpen ? 'UNLOCKED (SERVICE)' : 'ARMED & LOCKED'}</span>
          </div>
        </div>

        <div class="cassettes-grid">
          ${cassettesHtml}
        </div>
      </div>
    `;
  }

  _renderReplenish(state) {
    const vault = state.vaultStatus || {
      totalCash: 0,
      doorOpen: false,
      cassettes: []
    };

    store.setSideKeys({
      F1: {
        label: 'Vault Diagnostics',
        action: () => store.setState({ engineerView: ENGINEER_VIEWS.VAULT, alert: null })
      },
      F3: {
        label: 'Audit & Integrity',
        action: () => this._actions.openAuditView()
      },
      F5: {
        label: vault.doorOpen ? 'Lock Safe Door' : 'Open Safe Door',
        variant: 'primary',
        action: () => this._actions.toggleVaultDoor(!vault.doorOpen)
      },
      F6: {
        label: 'Reset Counters',
        action: () => this._actions.resetHardwareCounters()
      },
      F8: {
        label: 'Eject Service Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const cassettesHtml = vault.cassettes
      .map((c, idx) => {
        const pct = Math.min(100, Math.round((c.count / c.capacity) * 100));
        const remainingCap = Math.max(0, c.capacity - c.count);
        return `
          <div class="cassette-card">
            <div class="cassette-top">
              <span class="cassette-denom">${escapeHtml(formatDenomLabel(c.denom))} CASSETTE</span>
              <span class="cassette-pct">${escapeHtml(c.count)}/${escapeHtml(c.capacity)} (${pct}%)</span>
            </div>
            <div class="cassette-meter">
              <div class="cassette-meter-fill" style="width:${pct}%"></div>
            </div>
            <div class="cassette-meta">
              <button
                type="button"
                class="replenish-inline-btn"
                data-rep-idx="${idx}"
                data-rep-count="100"
                ${!vault.doorOpen || remainingCap < 100 || state.isBusy ? 'disabled' : ''}
              >
                +100 Notes
              </button>
              <button
                type="button"
                class="replenish-inline-btn"
                data-rep-idx="${idx}"
                data-rep-count="500"
                ${!vault.doorOpen || remainingCap < 500 || state.isBusy ? 'disabled' : ''}
              >
                +500 Notes
              </button>
              <button
                type="button"
                class="replenish-inline-btn"
                data-rep-idx="${idx}"
                data-rep-count="${remainingCap}"
                ${!vault.doorOpen || remainingCap === 0 || state.isBusy ? 'disabled' : ''}
              >
                Fill Max
              </button>
            </div>
          </div>
        `;
      })
      .join('');

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="eng-header-bar">
          <div>
            <h2 class="screen-title">CASH REPLENISHMENT &amp; CASSETTES</h2>
            <p class="screen-subtitle">
              ${
                vault.doorOpen
                  ? 'Safe door is OPEN. Select a cassette below to load banknotes.'
                  : 'Safe door is CLOSED. Press F5 or click "Open Safe for Replenishment" first.'
              }
            </p>
          </div>
          <button
            type="button"
            id="rep-toggle-door-btn"
            class="screen-btn ${vault.doorOpen ? '' : 'screen-btn-primary'}"
            ${state.isBusy ? 'disabled' : ''}
          >
            ${vault.doorOpen ? 'Lock Safe Door' : 'Open Safe for Replenishment'}
          </button>
        </div>

        <div class="cassettes-grid">
          ${cassettesHtml}
        </div>
      </div>
    `;

    qs('#rep-toggle-door-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.toggleVaultDoor(!vault.doorOpen);
    });

    this._viewportEl.querySelectorAll('[data-rep-idx]').forEach((btn) => {
      btn.addEventListener('click', () => {
        const cassetteIndex = Number(btn.getAttribute('data-rep-idx'));
        const addCount = Number(btn.getAttribute('data-rep-count'));
        this._actions.replenishCassette(cassetteIndex, addCount);
      });
    });
  }

  _renderAudit(state) {
    store.setSideKeys({
      F1: {
        label: 'Vault Diagnostics',
        action: () => store.setState({ engineerView: ENGINEER_VIEWS.VAULT, alert: null })
      },
      F2: {
        label: 'Replenish Cash',
        action: () => store.setState({ engineerView: ENGINEER_VIEWS.REPLENISH, alert: null })
      },
      F4: {
        label: 'Verify Integrity',
        variant: 'primary',
        action: () => this._actions.verifyAuditIntegrity()
      },
      F8: {
        label: 'Eject Service Card',
        variant: 'danger',
        action: () => this._actions.ejectCard()
      }
    });

    const integrity = state.integrityStatus;
    let integrityBannerHtml = '';
    if (integrity) {
      const cls = integrity.valid ? 'chain-valid' : 'chain-compromised';
      const statusTag = integrity.valid ? 'INTEGRITY VALID' : 'CHAIN COMPROMISED';
      integrityBannerHtml = `
        <div class="integrity-banner ${cls}">
          <span>[${statusTag}] ${escapeHtml(integrity.detail)}</span>
          <span>CHECKED: ${escapeHtml(integrity.checkedRecords)}</span>
        </div>
      `;
    }

    const rowsHtml =
      state.auditRecords.length === 0
        ? `<tr><td colspan="5">No audit records loaded.</td></tr>`
        : state.auditRecords
            .map(
              (rec) => `
                <tr class="${rec.tampered ? 'tampered-row' : ''}">
                  <td>#${escapeHtml(rec.id)}</td>
                  <td>${escapeHtml(rec.timestamp)}</td>
                  <td><strong>${escapeHtml(rec.event)}</strong> (${escapeHtml(rec.detail || '')})</td>
                  <td title="${escapeHtml(rec.prevHash)}">${shortHash(rec.prevHash, 8)}</td>
                  <td title="${escapeHtml(rec.hash)}">${shortHash(rec.hash, 8)}</td>
                </tr>
              `
            )
            .join('');

    const mockTamperBtnHtml = CONFIG.USE_MOCK
      ? `
        <button type="button" id="audit-sim-tamper-btn" class="screen-btn screen-btn-danger">
          Simulate/Restore DB Tamper
        </button>
      `
      : '';

    this._viewportEl.innerHTML = `
      <div class="screen-panel">
        <div class="eng-header-bar">
          <div>
            <h2 class="screen-title">CRYPTOGRAPHIC AUDIT LOG (SHA-256 + HMAC)</h2>
            <p class="screen-subtitle">Tamper-evident hash chain of all ledger and hardware events.</p>
          </div>
          <div style="display:flex; gap:6px;">
            ${mockTamperBtnHtml}
            <button
              type="button"
              id="audit-verify-btn"
              class="screen-btn screen-btn-primary"
              ${state.isBusy ? 'disabled' : ''}
            >
              Verify Hash Chain Integrity
            </button>
          </div>
        </div>

        ${integrityBannerHtml}

        <div class="audit-table-wrap">
          <table class="audit-table">
            <thead>
              <tr>
                <th>ID</th>
                <th>Timestamp</th>
                <th>EventType / Detail</th>
                <th>PreviousHash</th>
                <th>RecordHash</th>
              </tr>
            </thead>
            <tbody>
              ${rowsHtml}
            </tbody>
          </table>
        </div>
      </div>
    `;

    qs('#audit-verify-btn', this._viewportEl)?.addEventListener('click', () => {
      this._actions.verifyAuditIntegrity();
    });

    qs('#audit-sim-tamper-btn', this._viewportEl)?.addEventListener('click', () => {
      const tampered = toggleMockAuditTamper();
      this._actions.openAuditView(
        tampered
          ? { type: 'warn', message: 'Simulated attacker tampering on Record #2. Run Verify Integrity to test detection.' }
          : { type: 'info', message: 'Restored original Record #2 payload. Run Verify Integrity to confirm.' }
      );
    });
  }
}
