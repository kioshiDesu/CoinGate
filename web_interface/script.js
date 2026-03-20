const API = '';

let wizardStep = 1;
let systemConfig = null;

document.addEventListener('DOMContentLoaded', async function() {
    await checkSetup();
    initTabs();
    loadDashboard();
    
    setInterval(loadDashboard, 5000);
    
    document.getElementById('wifi-form').addEventListener('submit', saveWifi);
    document.getElementById('mikrotik-form').addEventListener('submit', saveMikroTik);
    document.getElementById('coin-form').addEventListener('submit', saveCoinSettings);
    document.getElementById('password-form').addEventListener('submit', changePassword);
});

async function checkSetup() {
    try {
        const res = await fetch(API + '/api/config');
        systemConfig = await res.json();
        
        if (!systemConfig.setup_done) {
            document.getElementById('setup-wizard').style.display = 'block';
            document.getElementById('main-tabs').style.display = 'none';
            document.getElementById('main-content').style.display = 'none';
        } else {
            loadConfig();
        }
    } catch (err) {
        console.error('Setup check failed:', err);
    }
}

function initTabs() {
    document.querySelectorAll('.tab-btn').forEach(btn => {
        btn.addEventListener('click', () => {
            const tabId = btn.dataset.tab;
            
            document.querySelectorAll('.tab-btn').forEach(b => b.classList.remove('active'));
            document.querySelectorAll('.tab-content').forEach(c => c.classList.remove('active'));
            
            btn.classList.add('active');
            document.getElementById(tabId).classList.add('active');
            
            if (tabId === 'dashboard') loadDashboard();
            if (tabId === 'sales') loadSales();
            if (tabId === 'history') loadHistory();
        });
    });
}

async function loadDashboard() {
    try {
        const res = await fetch(API + '/api/status');
        const data = await res.json();
        
        document.getElementById('stat-wifi').textContent = data.wifi ? 'Connected' : 'Disconnected';
        document.getElementById('stat-wifi').className = 'stat-value ' + (data.wifi ? 'success' : 'error');
        
        document.getElementById('stat-mt').textContent = data.mt_conn ? 'Connected' : 'Disconnected';
        document.getElementById('stat-mt').className = 'stat-value ' + (data.mt_conn ? 'success' : 'error');
        
        document.getElementById('stat-daily-v').textContent = data.daily_vouchers || 0;
        document.getElementById('stat-daily-p').textContent = data.daily_pulses || 0;
        
        document.getElementById('current-pulses').textContent = data.pulses_session || 0;
        
        const duration = calculateDuration(data.pulses_session || 0, data.cpp || 1);
        document.getElementById('current-duration').textContent = formatDuration(duration);
        
        document.getElementById('total-v').textContent = data.total_vouchers || 0;
        document.getElementById('total-p').textContent = data.total_pulses || 0;
        
        document.getElementById('abuse-status').textContent = 
            data.cooldown ? 'Cooldown' : (data.suspicious > 10 ? 'Warning' : 'OK');
        
        const seconds = data.uptime || 0;
        const h = Math.floor(seconds / 3600);
        const m = Math.floor((seconds % 3600) / 60);
        const s = seconds % 60;
        document.getElementById('uptime').textContent = `${h}:${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}`;
        
    } catch (err) {
        console.error('Dashboard load failed:', err);
    }
}

async function loadSales() {
    try {
        const res = await fetch(API + '/api/sales');
        const data = await res.json();
        
        document.getElementById('sales-daily-v').textContent = data.daily_vouchers || 0;
        document.getElementById('sales-daily-p').textContent = data.daily_pulses || 0;
        document.getElementById('sales-total-v').textContent = data.total_vouchers || 0;
        document.getElementById('sales-total-p').textContent = data.total_pulses || 0;
        
    } catch (err) {
        console.error('Sales load failed:', err);
    }
}

async function loadHistory() {
    try {
        const res = await fetch(API + '/api/history');
        const data = await res.json();
        
        const list = document.getElementById('history-list');
        
        if (!data || data.length === 0) {
            list.innerHTML = '<p class="empty-state">No vouchers generated yet</p>';
            return;
        }
        
        let html = '';
        data.slice().reverse().forEach(v => {
            const time = new Date(v.created_at * 1000).toLocaleString();
            html += `
                <div class="history-item">
                    <span class="username">${v.username}</span>
                    <span class="password">${v.password}</span>
                    <span class="duration">${formatDuration(v.duration)}</span>
                    <span class="time">${time}</span>
                </div>
            `;
        });
        
        list.innerHTML = html;
        
    } catch (err) {
        console.error('History load failed:', err);
        document.getElementById('history-list').innerHTML = '<p class="empty-state">Failed to load history</p>';
    }
}

async function loadConfig() {
    try {
        const res = await fetch(API + '/api/config');
        const data = await res.json();
        
        document.getElementById('wifi-ssid').value = data.wifi_ssid || '';
        document.getElementById('mt-host').value = data.mt_host || '';
        document.getElementById('mt-port').value = data.mt_port || 8728;
        document.getElementById('mt-user').value = data.mt_user || '';
        document.getElementById('coin-cpp').value = data.cpp || 1;
        document.getElementById('coin-min').value = data.min || 1;
        document.getElementById('coin-max').value = data.max || 100;
        
    } catch (err) {
        console.error('Config load failed:', err);
    }
}

async function generateVoucher() {
    try {
        const res = await fetch(API + '/api/voucher', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({})
        });
        
        const data = await res.json();
        
        if (data.success) {
            showModal('Voucher Generated!', 
                `Username: ${data.username}<br>Password: ${data.password}<br>Duration: ${formatDuration(data.duration)}`);
            loadDashboard();
        } else {
            showModal('Error', data.error || 'Failed to generate voucher');
        }
        
    } catch (err) {
        showModal('Error', 'Failed to generate voucher');
    }
}

async function generateManualVoucher() {
    const coins = parseInt(document.getElementById('voucher-coins').value) || 10;
    
    try {
        const res = await fetch(API + '/api/voucher', {
            method: 'POST',
            headers: {'Content-Type': 'application/json'},
            body: JSON.stringify({coins: coins})
        });
        
        const data = await res.json();
        
        if (data.success) {
            document.getElementById('v-username').textContent = data.username;
            document.getElementById('v-password').textContent = data.password;
            document.getElementById('v-duration').textContent = formatDuration(data.duration);
            document.getElementById('voucher-result').style.display = 'block';
            document.querySelector('.voucher-generator').style.display = 'none';
            loadDashboard();
        } else {
            showModal('Error', data.error || 'Failed to generate voucher');
        }
        
    } catch (err) {
        showModal('Error', 'Failed to generate voucher');
    }
}

function clearVoucher() {
    document.getElementById('voucher-result').style.display = 'none';
    document.querySelector('.voucher-generator').style.display = 'block';
    document.getElementById('voucher-coins').value = 10;
}

async function resetSession() {
    if (!confirm('Reset current session?')) return;
    
    await fetch(API + '/api/reset', {method: 'POST'});
    loadDashboard();
}

async function resetDailySales() {
    if (!confirm('Reset daily sales counter?')) return;
    
    await fetch(API + '/api/sales/reset', {method: 'POST'});
    loadSales();
}

async function saveWifi(e) {
    e.preventDefault();
    
    const ssid = document.getElementById('wifi-ssid').value;
    const pass = document.getElementById('wifi-pass').value;
    
    const formData = new URLSearchParams();
    formData.append('ssid', ssid);
    if (pass) formData.append('password', pass);
    
    const res = await fetch(API + '/api/wifi', {
        method: 'POST',
        body: formData
    });
    
    if (res.ok) {
        showModal('Saved', 'WiFi settings saved. Reconnecting...');
    }
}

async function saveMikroTik(e) {
    e.preventDefault();
    
    const config = {
        host: document.getElementById('mt-host').value,
        port: parseInt(document.getElementById('mt-port').value),
        username: document.getElementById('mt-user').value,
        password: document.getElementById('mt-pass').value
    };
    
    const res = await fetch(API + '/api/mikrotik', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    });
    
    const data = await res.json();
    
    if (data.success) {
        showModal('Saved', 'MikroTik settings saved.');
    } else {
        showModal('Error', data.error || 'Failed to save');
    }
}

async function testMikroTik() {
    showModal('Testing...', 'Testing MikroTik connection...');
    
    const res = await fetch(API + '/api/mikrotik/test', {method: 'POST'});
    const data = await res.json();
    
    showModal(data.success ? 'Success' : 'Error', data.message || data.error);
}

async function saveCoinSettings(e) {
    e.preventDefault();
    
    const config = {
        cpp: parseInt(document.getElementById('coin-cpp').value),
        min: parseInt(document.getElementById('coin-min').value),
        max: parseInt(document.getElementById('coin-max').value)
    };
    
    const res = await fetch(API + '/api/coin/config', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    });
    
    if (res.ok) {
        showModal('Saved', 'Coin settings saved.');
    }
}

async function changePassword(e) {
    e.preventDefault();
    
    const current = document.getElementById('pass-current').value;
    const newPass = document.getElementById('pass-new').value;
    const confirm = document.getElementById('pass-confirm').value;
    
    if (newPass !== confirm) {
        showModal('Error', 'Passwords do not match');
        return;
    }
    
    const res = await fetch(API + '/api/password', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({current: current, new: newPass})
    });
    
    const data = await res.json();
    
    if (data.success) {
        document.getElementById('password-form').reset();
        showModal('Success', 'Password changed successfully');
    } else {
        showModal('Error', data.error || 'Failed to change password');
    }
}

async function rebootSystem() {
    if (!confirm('Reboot the system?')) return;
    
    showModal('Rebooting', 'System is rebooting...');
    await fetch(API + '/api/reboot', {method: 'POST'});
}

async function factoryReset() {
    if (!confirm('Factory reset will erase all settings. Continue?')) return;
    if (!confirm('This cannot be undone. Are you absolutely sure?')) return;
    
    showModal('Resetting', 'Factory reset in progress...');
    await fetch(API + '/api/factory_reset', {method: 'POST'});
}

function wizardNext() {
    document.querySelector(`.wizard-step[data-step="${wizardStep}"]`).classList.remove('active');
    wizardStep++;
    document.querySelector(`.wizard-step[data-step="${wizardStep}"]`).classList.add('active');
}

function wizardPrev() {
    document.querySelector(`.wizard-step[data-step="${wizardStep}"]`).classList.remove('active');
    wizardStep--;
    document.querySelector(`.wizard-step[data-step="${wizardStep}"]`).classList.add('active');
}

async function wizardSaveWifi() {
    const ssid = document.getElementById('w-wifi-ssid').value;
    const pass = document.getElementById('w-wifi-pass').value;
    
    if (!ssid) {
        showModal('Error', 'Please enter WiFi SSID');
        return;
    }
    
    const formData = new URLSearchParams();
    formData.append('ssid', ssid);
    if (pass) formData.append('password', pass);
    
    await fetch(API + '/api/wifi', {method: 'POST', body: formData});
    wizardNext();
}

async function wizardSaveMikroTik() {
    const config = {
        host: document.getElementById('w-mt-host').value,
        port: parseInt(document.getElementById('w-mt-port').value) || 8728,
        username: document.getElementById('w-mt-user').value,
        password: document.getElementById('w-mt-pass').value
    };
    
    if (!config.host || !config.username) {
        showModal('Error', 'Please fill in all MikroTik fields');
        return;
    }
    
    await fetch(API + '/api/mikrotik', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(config)
    });
    
    wizardNext();
}

async function wizardSavePassword() {
    const pass1 = document.getElementById('w-admin-pass').value;
    const pass2 = document.getElementById('w-admin-pass2').value;
    
    if (!pass1 || pass1.length < 4) {
        showModal('Error', 'Password must be at least 4 characters');
        return;
    }
    
    if (pass1 !== pass2) {
        showModal('Error', 'Passwords do not match');
        return;
    }
    
    await fetch(API + '/api/password', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({current: 'admin', new: pass1})
    });
    
    await fetch(API + '/api/setup', {method: 'POST'});
    
    document.getElementById('setup-wizard').style.display = 'none';
    document.getElementById('main-tabs').style.display = 'flex';
    document.getElementById('main-content').style.display = 'block';
    
    loadConfig();
}

function showModal(title, message) {
    document.getElementById('modal-title').textContent = title;
    document.getElementById('modal-message').innerHTML = message;
    document.getElementById('modal-actions').innerHTML = '<button class="btn btn-primary" onclick="closeModal()">OK</button>';
    document.getElementById('modal').classList.add('active');
}

function closeModal() {
    document.getElementById('modal').classList.remove('active');
}

function calculateDuration(coins, cpp) {
    return Math.floor((coins / cpp) * 600);
}

function formatDuration(seconds) {
    if (seconds < 60) return `${seconds} sec`;
    if (seconds < 3600) return `${Math.floor(seconds / 60)} min`;
    const h = Math.floor(seconds / 3600);
    const m = Math.floor((seconds % 3600) / 60);
    return m > 0 ? `${h}h ${m}m` : `${h} hour${h > 1 ? 's' : ''}`;
}
