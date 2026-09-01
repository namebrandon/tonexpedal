#!/usr/bin/env node

const fs = require('node:fs');
const http = require('node:http');
const path = require('node:path');
const { WebSocketServer, WebSocket } = require('ws');

const ROOT_DIR = path.join(__dirname, '..');
const TOTAL_PRESETS = 150;
const SLOTS = ['A', 'B', 'C'];
const CONFIRMATION_MODES = new Set(['normal', 'drop', 'error']);
const SUPPORTED_RIG_CONTROLS = new Set([14, 18, 75, 117, 122]);

function validateWifiSettings(settings) {
    if (typeof settings.ssid !== 'string' || settings.ssid.length === 0) {
        return { field: 'ssid', message: 'Network name is required' };
    }
    if (Buffer.byteLength(settings.ssid, 'utf8') > 32) {
        return { field: 'ssid', message: 'Network name must be 32 bytes or fewer' };
    }
    if (/[\x00-\x1f\x7f]/.test(settings.ssid)) {
        return { field: 'ssid', message: 'Network name cannot contain control characters' };
    }
    const password = typeof settings.password === 'string' ? settings.password : '';
    if (settings.open_network && password.length > 0) {
        return { field: 'password', message: 'An open network must not include a password' };
    }
    const passwordBytes = Buffer.byteLength(password, 'utf8');
    if (!settings.open_network && passwordBytes < 8) {
        return { field: 'password', message: 'Wi-Fi password must contain at least 8 bytes' };
    }
    if (passwordBytes > 63) {
        return { field: 'password', message: 'Wi-Fi password must contain no more than 63 bytes' };
    }
    if (/[\x00-\x1f\x7f]/.test(password)) {
        return { field: 'password', message: 'Wi-Fi password cannot contain control characters' };
    }
    if (typeof settings.hostname !== 'string' || settings.hostname.length === 0) {
        return { field: 'hostname', message: 'Device name is required' };
    }
    if (settings.hostname.length > 63 || !/^[A-Za-z0-9](?:[A-Za-z0-9-]*[A-Za-z0-9])?$/.test(settings.hostname)) {
        return { field: 'hostname', message: 'Device name may contain only letters, numbers, and interior hyphens' };
    }
    return null;
}

function presetLocation(pc) {
    return { bank: Math.floor(pc / 3), slot: SLOTS[pc % 3] };
}

function presetInfo(pc) {
    const { bank, slot } = presetLocation(pc);
    return {
        event: 'preset_update',
        bank,
        slot,
        name: `Simulated ${String(bank).padStart(2, '0')}${slot}`,
        amp: pc % 4 !== 0,
        cab: pc % 5 !== 0
    };
}

function jsonResponse(response, status, body) {
    const payload = JSON.stringify(body);
    response.writeHead(status, {
        'Content-Type': 'application/json; charset=utf-8',
        'Content-Length': Buffer.byteLength(payload),
        'Cache-Control': 'no-store'
    });
    response.end(payload);
}

function readJson(request) {
    return new Promise((resolve, reject) => {
        const chunks = [];
        let size = 0;
        request.on('data', chunk => {
            size += chunk.length;
            if (size > 64 * 1024) {
                reject(new Error('Request body is too large'));
                request.destroy();
                return;
            }
            chunks.push(chunk);
        });
        request.on('end', () => {
            try {
                resolve(JSON.parse(Buffer.concat(chunks).toString('utf8') || '{}'));
            } catch (error) {
                reject(new Error('Request body must be valid JSON'));
            }
        });
        request.on('error', reject);
    });
}

function createBridgeSimulator(options = {}) {
    const host = options.host || '127.0.0.1';
    const requestedPort = options.port ?? 8787;
    const confirmationDelayMs = options.confirmationDelayMs ?? 80;
    const syncStepMs = options.syncStepMs ?? 8;
    const frontendDir = options.frontendDir || ROOT_DIR;

    const state = {
        tonexConnected: options.tonexConnected !== false,
        activePc: Number.isInteger(options.activePc) ? options.activePc : 0,
        confirmationMode: 'normal',
        clients: new Map(),
        nextClientId: 1,
        sync: null
    };
    state.wifi = {
        setupMode: options.wifiSetupMode === true,
        stored: false,
        ssid: '',
        password: '',
        hostname: 'tonex',
        openNetwork: false
    };

    const staticFiles = new Map([
        ['/index.html', ['index.html', 'text/html; charset=utf-8']],
        ['/setup.html', ['setup.html', 'text/html; charset=utf-8']],
        ['/ui.css', ['ui.css', 'text/css; charset=utf-8']],
        ['/setup.css', ['setup.css', 'text/css; charset=utf-8']],
        ['/favicon.svg', ['favicon.svg', 'image/svg+xml']]
    ]);

    function publicState() {
        return {
            service: 'tonex-bridge-simulator',
            tonex_connected: state.tonexConnected,
            active_pc: state.activePc,
            confirmation_mode: state.confirmationMode,
            connected_clients: state.clients.size,
            sync: state.sync ? {
                owner_client_id: state.sync.ownerId,
                loaded: state.sync.loaded,
                total: TOTAL_PRESETS
            } : null
        };
    }

    function publicWifiState() {
        return {
            setup_mode: state.wifi.setupMode,
            configured: state.wifi.ssid.length > 0,
            stored: state.wifi.stored,
            ssid: state.wifi.ssid,
            hostname: state.wifi.hostname,
            open_network: state.wifi.openNetwork,
            station_connected: !state.wifi.setupMode && state.wifi.ssid.length > 0,
            setup_ssid: state.wifi.setupMode ? 'TONEX-Setup-SIM001' : undefined,
            setup_ip: state.wifi.setupMode ? '192.168.4.1' : undefined
        };
    }

    function send(socket, message) {
        if (socket.readyState === WebSocket.OPEN) {
            socket.send(JSON.stringify(message));
        }
    }

    function broadcast(message) {
        for (const socket of state.clients.keys()) send(socket, message);
    }

    function statusMessage() {
        const message = {
            event: 'status',
            tonex_connected: state.tonexConnected
        };
        if (state.tonexConnected && state.activePc >= 0) {
            const location = presetLocation(state.activePc);
            message.active_pc = state.activePc;
            message.active_bank = location.bank;
            message.active_slot = location.slot;
        }
        return message;
    }

    function sendError(socket, code, message, requestId) {
        const event = { event: 'error', code, message };
        if (requestId !== undefined) event.request_id = requestId;
        send(socket, event);
    }

    function clearSync() {
        if (!state.sync) return;
        clearTimeout(state.sync.timer);
        state.sync = null;
    }

    function runSyncStep() {
        const sync = state.sync;
        if (!sync || !state.tonexConnected) return;

        if (sync.loaded >= TOTAL_PRESETS) {
            const completion = { event: 'sync_complete', total: TOTAL_PRESETS };
            if (sync.requestId !== undefined) completion.request_id = sync.requestId;
            clearSync();
            broadcast(completion);
            return;
        }

        broadcast(presetInfo(sync.loaded));
        sync.loaded += 1;
        broadcast({
            event: 'sync_progress',
            loaded: sync.loaded,
            total: TOTAL_PRESETS,
            percent: Math.floor(sync.loaded * 100 / TOTAL_PRESETS),
            owner_client_id: sync.ownerId
        });
        sync.timer = setTimeout(runSyncStep, syncStepMs);
    }

    function handleMessage(socket, raw) {
        let message;
        try {
            message = JSON.parse(raw.toString());
        } catch (error) {
            sendError(socket, 'invalid_json', 'Bridge commands must be valid JSON');
            return;
        }

        const clientId = state.clients.get(socket);
        const requestId = message.request_id;

        if (message.action === 'status_request') {
            send(socket, statusMessage());
            return;
        }

        if (message.action === 'midi_send') {
            const bank = Number(message.bank);
            const channel = Number(message.channel);
            const slot = message.slot;
            if (!Number.isInteger(bank) || bank < 0 || bank >= 50 ||
                !SLOTS.includes(slot) || !Number.isInteger(channel) || channel < 0 || channel >= 16) {
                sendError(socket, 'midi_invalid', 'Invalid preset or MIDI channel', requestId);
                return;
            }
            if (!state.tonexConnected) {
                sendError(socket, 'midi_unavailable', 'The TONEX MIDI interface is not ready', requestId);
                return;
            }

            const pc = bank * 3 + SLOTS.indexOf(slot);
            send(socket, { event: 'midi_accepted', pc, request_id: requestId });
            if (state.confirmationMode === 'drop') return;
            if (state.confirmationMode === 'error') {
                setTimeout(() => sendError(
                    socket, 'midi_simulated_failure', 'Simulated pedal confirmation failure', requestId
                ), confirmationDelayMs);
                return;
            }
            setTimeout(() => {
                if (!state.tonexConnected) return;
                state.activePc = pc;
                broadcast(statusMessage());
            }, confirmationDelayMs);
            return;
        }

        if (message.action === 'midi_cc') {
            const control = Number(message.cc);
            const value = Number(message.value);
            const channel = Number(message.channel);
            if (!SUPPORTED_RIG_CONTROLS.has(control) || !Number.isInteger(value) || value < 0 || value > 127 ||
                !Number.isInteger(channel) || channel < 0 || channel >= 16) {
                sendError(socket, 'midi_invalid', 'Unsupported MIDI control, value, or channel', requestId);
                return;
            }
            if (!state.tonexConnected) {
                sendError(socket, 'midi_unavailable', 'The TONEX MIDI interface is not ready', requestId);
                return;
            }
            send(socket, { event: 'midi_cc_accepted', cc: control, value, request_id: requestId });
            return;
        }

        if (message.action === 'sync_start') {
            if (!state.tonexConnected) {
                sendError(socket, 'sync_unavailable', 'The TONEX is disconnected', requestId);
                return;
            }
            if (state.sync) {
                sendError(socket, 'sync_busy', 'Preset sync is owned by another browser', requestId);
                return;
            }
            state.sync = { owner: socket, ownerId: clientId, requestId, loaded: 0, timer: null };
            send(socket, { event: 'sync_started', total: TOTAL_PRESETS, request_id: requestId });
            runSyncStep();
            return;
        }

        if (message.action === 'sync_cancel') {
            if (!state.sync) {
                sendError(socket, 'sync_not_active', 'No preset sync is currently active', requestId);
                return;
            }
            if (state.sync.owner !== socket) {
                sendError(socket, 'sync_not_owner', 'Only the browser that started sync can cancel it', requestId);
                return;
            }
            const cancellation = { event: 'sync_cancelled', request_id: state.sync.requestId };
            clearSync();
            broadcast(cancellation);
            return;
        }

        sendError(socket, 'unknown_action', 'Unknown bridge action', requestId);
    }

    async function applyControl(action) {
        switch (action.action) {
        case 'pedal_connect':
            state.tonexConnected = true;
            broadcast(statusMessage());
            break;
        case 'pedal_disconnect':
            state.tonexConnected = false;
            clearSync();
            broadcast(statusMessage());
            break;
        case 'set_confirmation_mode':
            if (!CONFIRMATION_MODES.has(action.mode)) {
                throw new Error('mode must be normal, drop, or error');
            }
            state.confirmationMode = action.mode;
            break;
        case 'close_clients':
            for (const socket of state.clients.keys()) socket.close(1012, 'Simulated network interruption');
            break;
        case 'reset':
            clearSync();
            state.tonexConnected = true;
            state.activePc = 0;
            state.confirmationMode = 'normal';
            broadcast(statusMessage());
            break;
        case 'wifi_setup_start':
            state.wifi.setupMode = true;
            break;
        case 'wifi_setup_stop':
            state.wifi.setupMode = false;
            break;
        default:
            throw new Error('Unknown simulator action');
        }
        return publicState();
    }

    const server = http.createServer(async (request, response) => {
        const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`);

        if (request.method === 'GET' && url.pathname === '/api/bridge') {
            jsonResponse(response, 200, {
                service: 'tonex-bridge',
                protocol_version: 1,
                simulated: true,
                setup_mode: state.wifi.setupMode
            });
            return;
        }
        if (request.method === 'GET' && url.pathname === '/api/wifi') {
            jsonResponse(response, 200, publicWifiState());
            return;
        }
        if (request.method === 'POST' && url.pathname === '/api/wifi') {
            try {
                const settings = await readJson(request);
                if (!state.wifi.setupMode) {
                    jsonResponse(response, 403, {
                        error: 'setup_required',
                        message: 'Wi-Fi changes are available only in setup mode'
                    });
                    return;
                }
                const validation = validateWifiSettings(settings);
                if (validation) {
                    jsonResponse(response, 400, { error: 'validation_failed', ...validation });
                    return;
                }
                state.wifi.ssid = settings.ssid;
                state.wifi.password = settings.open_network ? '' : settings.password;
                state.wifi.hostname = settings.hostname;
                state.wifi.openNetwork = !!settings.open_network;
                state.wifi.stored = true;
                jsonResponse(response, 202, {
                    accepted: true,
                    restarting: true,
                    hostname: state.wifi.hostname,
                    request_id: settings.request_id
                });
            } catch (error) {
                jsonResponse(response, 400, { error: 'invalid_json', message: error.message });
            }
            return;
        }
        if (request.method === 'DELETE' && url.pathname === '/api/wifi') {
            if (!state.wifi.setupMode) {
                jsonResponse(response, 403, { error: 'setup_required' });
                return;
            }
            state.wifi.stored = false;
            state.wifi.ssid = '';
            state.wifi.password = '';
            state.wifi.hostname = 'tonex';
            state.wifi.openNetwork = false;
            jsonResponse(response, 202, { accepted: true, restarting: true });
            return;
        }
        if (request.method === 'GET' && url.pathname === '/api/simulator') {
            jsonResponse(response, 200, publicState());
            return;
        }
        if (request.method === 'POST' && url.pathname === '/api/simulator') {
            try {
                jsonResponse(response, 200, await applyControl(await readJson(request)));
            } catch (error) {
                jsonResponse(response, 400, { error: error.message });
            }
            return;
        }

        const asset = url.pathname === '/' ?
            (state.wifi.setupMode ? ['setup.html', 'text/html; charset=utf-8'] : ['index.html', 'text/html; charset=utf-8']) :
            staticFiles.get(url.pathname);
        if (request.method === 'GET' && asset) {
            const [relativePath, contentType] = asset;
            fs.readFile(path.join(frontendDir, relativePath), (error, data) => {
                if (error) {
                    response.writeHead(404);
                    response.end('Not found');
                    return;
                }
                response.writeHead(200, { 'Content-Type': contentType, 'Cache-Control': 'no-store' });
                response.end(data);
            });
            return;
        }

        response.writeHead(404, { 'Content-Type': 'text/plain; charset=utf-8' });
        response.end('Not found');
    });

    const websocketServer = new WebSocketServer({ noServer: true });
    server.on('upgrade', (request, socket, head) => {
        const url = new URL(request.url, `http://${request.headers.host || 'localhost'}`);
        if (url.pathname !== '/ws') {
            socket.destroy();
            return;
        }
        websocketServer.handleUpgrade(request, socket, head, ws => websocketServer.emit('connection', ws));
    });

    websocketServer.on('connection', socket => {
        const clientId = state.nextClientId++;
        state.clients.set(socket, clientId);
        send(socket, { ...statusMessage(), client_id: clientId });
        socket.on('message', raw => handleMessage(socket, raw));
        socket.on('close', () => {
            if (state.sync && state.sync.owner === socket) {
                clearSync();
                broadcast({ event: 'sync_cancelled', reason: 'owner_disconnected' });
            }
            state.clients.delete(socket);
        });
    });

    return {
        state,
        server,
        websocketServer,
        applyControl,
        async start() {
            await new Promise((resolve, reject) => {
                server.once('error', reject);
                server.listen(requestedPort, host, () => {
                    server.off('error', reject);
                    resolve();
                });
            });
            return server.address();
        },
        async stop() {
            clearSync();
            for (const socket of state.clients.keys()) socket.terminate();
            await new Promise(resolve => websocketServer.close(resolve));
            if (server.listening) await new Promise(resolve => server.close(resolve));
        }
    };
}

function parseArgs(argv) {
    const options = {};
    for (let index = 0; index < argv.length; index += 1) {
        const arg = argv[index];
        if (arg === '--port') options.port = Number(argv[++index]);
        else if (arg === '--host') options.host = argv[++index];
        else if (arg === '--disconnected') options.tonexConnected = false;
        else if (arg === '--setup') options.wifiSetupMode = true;
        else if (arg === '--help') options.help = true;
        else throw new Error(`Unknown argument: ${arg}`);
    }
    if (options.port !== undefined && (!Number.isInteger(options.port) || options.port < 0 || options.port > 65535)) {
        throw new Error('--port must be an integer from 0 through 65535');
    }
    return options;
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
        console.log('Usage: node tools/bridge_simulator.js [--host 127.0.0.1] [--port 8787] [--disconnected] [--setup]');
        return;
    }

    const simulator = createBridgeSimulator(options);
    const address = await simulator.start();
    const displayHost = address.address === '0.0.0.0' ? 'localhost' : address.address;
    console.log(`TONEX bridge simulator: http://${displayHost}:${address.port}`);
    console.log('Control API: POST /api/simulator (pedal_connect, pedal_disconnect, set_confirmation_mode, close_clients, wifi_setup_start, wifi_setup_stop, reset)');

    const shutdown = async () => {
        await simulator.stop();
        process.exit(0);
    };
    process.once('SIGINT', shutdown);
    process.once('SIGTERM', shutdown);
}

if (require.main === module) {
    main().catch(error => {
        console.error(error.message);
        process.exitCode = 1;
    });
}

module.exports = { createBridgeSimulator, parseArgs, presetInfo, presetLocation, validateWifiSettings };
