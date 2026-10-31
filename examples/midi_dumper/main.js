/// <reference path="./budo.d.ts" />

import { createUI } from './ui/budo-ui.js';

const ui = createUI();
ui.setTheme('system');

const messages = [];
let devices = [];
let nextId = 1;
let selectedId = -1;
let deviceNames = {};

sys.log("MIDI Dumper");

sys.midi.onDevicesChanged(event => {
    onDevices(event.inputs, event.outputs);
});
sys.midi.refreshDevices();
onDevices(sys.midi.getInputDevices(), sys.midi.getOutputDevices());

function onDevices(inputs, outputs) {
    deviceNames = {};
    devices = [];

    for (let device of inputs) {
        sys.log("Input MIDI device:", JSON.stringify(device));
        deviceNames[device.id] = device.name;
        devices.push({ id: device.id, device: device.name });

        sys.midi.openInput(device.id, msg => {
            sys.log("MIDI message from device", device.id, ":", JSON.stringify(msg));
            messages.push({ id: nextId++, deviceId: device.id, device: deviceNames[device.id], status: msg.status, type: msg.type, channel: msg.channel, data1: msg.data1, data2: msg.data2 });
            sys.animation.requestFrame(frame);
        });
    }

    for (let device of outputs) {
        sys.log("Output MIDI device:", JSON.stringify(device));
    }
}

function drawDevices(rect) {
    ui.table('devices', rect, {
        // Column widths are dp, or { weight } shares; Size is right-aligned.
        columns: [
            { key: 'id', label: 'Device ID', align: 'left' },
            { key: 'device', label: 'Device' },
        ],
        // The table asks for the rows it shows, by index.
        rowCount: devices.length,
        row: i => devices[i],
        rowId: i => devices[i].id,
        // The app owns the selection and the sort order.
        selectedId,
    });
}

function drawMessages(rect) {
    ui.table('messages', rect, {
        // Column widths are dp, or { weight } shares; Size is right-aligned.
        columns: [
            { key: 'deviceId', label: 'Device ID', align: 'left' },
            { key: 'device', label: 'Device' },
            { key: 'status', label: 'Status', align: 'left' },
            { key: 'type', label: 'Type', align: 'left' },
            { key: 'channel', label: 'Channel', align: 'left' },
            { key: 'data1', label: 'Data 1', align: 'left' },
            { key: 'data2', label: 'Data 2', align: 'left' },
        ],
        // The table asks for the rows it shows, by index.
        rowCount: messages.length,
        row: i => messages[i],
        rowId: i => messages[i].id,
        // The app owns the selection and the sort order.
        selectedId,
    });
}

function frame() {
    ui.begin(sys.input.get());
    ui.clear();

    //const [devicesRow, messagesRow] = ui.rows(ui.bounds, [30, 30])
    ui.split('workspace', ui.bounds, drawDevices, drawMessages, { ratio: 0.33 });

    ui.end();
    ui.nextFrame(frame); // the next frame while something moves, otherwise on the next input
}

sys.animation.requestFrame(frame);