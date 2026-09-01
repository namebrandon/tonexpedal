const { afterEach, describe, it } = require('node:test');
const assert = require('node:assert');
const { WebSocket } = require('ws');
const { createBridgeSimulator } = require('../tools/bridge_simulator');

const running = [];

afterEach(async () => {
    await Promise.all(running.splice(0).map(simulator => simulator.stop()));
});

async function startSimulator(options = {}) {
    const simulator = createBridgeSimulator({ port: 0, confirmationDelayMs: 10, syncStepMs: 20, ...options });
    const address = await simulator.start();
    running.push(simulator);
    return { simulator, baseUrl: `http://127.0.0.1:${address.port}` };
}

function connect(baseUrl) {
    return new Promise((resolve, reject) => {
        const messages = [];
        const socket = new WebSocket(baseUrl.replace('http:', 'ws:') + '/ws');
        socket.on('message', raw => messages.push(JSON.parse(raw.toString())));
        socket.once('open', () => resolve({ socket, messages }));
        socket.once('error', reject);
    });
}

async function waitFor(messages, predicate, timeoutMs = 1000) {
    const started = Date.now();
    while (Date.now() - started < timeoutMs) {
        const match = messages.find(predicate);
        if (match) return match;
        await new Promise(resolve => setTimeout(resolve, 5));
    }
    assert.fail(`Timed out waiting for message. Received: ${JSON.stringify(messages)}`);
}

describe('Desktop bridge simulator', () => {
    it('serves bridge discovery and the production frontend', async () => {
        const { baseUrl } = await startSimulator();

        const identity = await (await fetch(baseUrl + '/api/bridge')).json();
        assert.deepStrictEqual(identity, {
            service: 'tonex-bridge',
            protocol_version: 1,
            simulated: true
        });

        const frontend = await (await fetch(baseUrl + '/')).text();
        assert.match(frontend, /TONEX Pedal Controller/);
        assert.match(frontend, /initWebSocketBridge/);
    });

    it('keeps acknowledgements private and broadcasts confirmed pedal state', async () => {
        const { baseUrl } = await startSimulator();
        const first = await connect(baseUrl);
        const second = await connect(baseUrl);
        await waitFor(first.messages, message => message.event === 'status');
        await waitFor(second.messages, message => message.event === 'status');
        first.messages.length = 0;
        second.messages.length = 0;

        first.socket.send(JSON.stringify({
            action: 'midi_send', bank: 1, slot: 'B', channel: 0, request_id: 7
        }));

        const accepted = await waitFor(first.messages, message => message.event === 'midi_accepted');
        assert.strictEqual(accepted.request_id, 7);
        assert.strictEqual(accepted.pc, 4);
        await waitFor(second.messages, message => message.event === 'status' && message.active_pc === 4);
        assert.strictEqual(second.messages.some(message => message.event === 'midi_accepted'), false);
    });

    it('simulates dropped confirmation and pedal disconnect recovery', async () => {
        const { simulator, baseUrl } = await startSimulator();
        const client = await connect(baseUrl);
        await waitFor(client.messages, message => message.event === 'status');
        client.messages.length = 0;

        await simulator.applyControl({ action: 'set_confirmation_mode', mode: 'drop' });
        client.socket.send(JSON.stringify({
            action: 'midi_send', bank: 2, slot: 'C', channel: 0, request_id: 8
        }));
        await waitFor(client.messages, message => message.event === 'midi_accepted');
        await new Promise(resolve => setTimeout(resolve, 30));
        assert.strictEqual(client.messages.some(message => message.active_pc === 8), false);

        await simulator.applyControl({ action: 'pedal_disconnect' });
        await waitFor(client.messages, message => message.event === 'status' && !message.tonex_connected);
        await simulator.applyControl({ action: 'pedal_connect' });
        await waitFor(client.messages, message => message.event === 'status' && message.tonex_connected);
    });

    it('enforces sync ownership between browsers', async () => {
        const { baseUrl } = await startSimulator();
        const owner = await connect(baseUrl);
        const observer = await connect(baseUrl);
        owner.socket.send(JSON.stringify({ action: 'sync_start', request_id: 11 }));
        await waitFor(owner.messages, message => message.event === 'sync_started');

        observer.socket.send(JSON.stringify({ action: 'sync_cancel', request_id: 12 }));
        const error = await waitFor(observer.messages, message => message.code === 'sync_not_owner');
        assert.strictEqual(error.request_id, 12);

        owner.socket.send(JSON.stringify({ action: 'sync_cancel', request_id: 11 }));
        await waitFor(owner.messages, message => message.event === 'sync_cancelled');
        await waitFor(observer.messages, message => message.event === 'sync_cancelled');
    });

    it('exposes deterministic runtime controls over HTTP', async () => {
        const { baseUrl } = await startSimulator();
        const response = await fetch(baseUrl + '/api/simulator', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ action: 'set_confirmation_mode', mode: 'error' })
        });
        assert.strictEqual(response.status, 200);
        const state = await response.json();
        assert.strictEqual(state.confirmation_mode, 'error');
    });
});
