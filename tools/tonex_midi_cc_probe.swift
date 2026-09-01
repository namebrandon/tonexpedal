#!/usr/bin/env swift

import CoreMIDI
import Darwin
import Foundation

struct ProbeArguments {
    let controller: UInt8
    let values: [UInt8]
    let restoreValue: UInt8
    let delay: TimeInterval
}

enum ProbeFailure: Error, CustomStringConvertible {
    case message(String)

    var description: String {
        switch self {
        case .message(let text): return text
        }
    }
}

func byteValue(_ text: String) -> UInt8? {
    guard let value = Int(text), (0...127).contains(value) else { return nil }
    return UInt8(value)
}

func parseArguments() -> ProbeArguments? {
    let arguments = Array(CommandLine.arguments.dropFirst())
    var controller: UInt8?
    var values: [UInt8]?
    var restoreValue: UInt8?
    var delay: TimeInterval = 1.5
    var offset = 0

    while offset < arguments.count {
        guard offset + 1 < arguments.count else { return nil }
        let option = arguments[offset]
        let value = arguments[offset + 1]
        switch option {
        case "--controller":
            guard controller == nil, let parsed = byteValue(value) else { return nil }
            controller = parsed
        case "--values":
            let components = value.split(separator: ",", omittingEmptySubsequences: false)
            let parsed = components.compactMap { byteValue(String($0)) }
            guard values == nil, !parsed.isEmpty, parsed.count == components.count else {
                return nil
            }
            values = parsed
        case "--restore-value":
            guard restoreValue == nil, let parsed = byteValue(value) else { return nil }
            restoreValue = parsed
        case "--delay":
            guard let parsed = TimeInterval(value), parsed >= 0.05 else { return nil }
            delay = parsed
        default:
            return nil
        }
        offset += 2
    }

    guard let controller, let values, let restoreValue else { return nil }
    return ProbeArguments(
        controller: controller,
        values: values,
        restoreValue: restoreValue,
        delay: delay
    )
}

func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value) == noErr,
          let value else {
        return "Unnamed MIDI endpoint"
    }
    return value.takeRetainedValue() as String
}

func matchingDestinations() -> [(MIDIEndpointRef, String)] {
    (0..<MIDIGetNumberOfDestinations()).compactMap { index in
        let endpoint = MIDIGetDestination(index)
        let name = endpointName(endpoint)
        return name.localizedCaseInsensitiveContains("tonex") ? (endpoint, name) : nil
    }
}

func sendControlChange(
    controller: UInt8,
    value: UInt8,
    port: MIDIPortRef,
    destination: MIDIEndpointRef
) throws {
    let packetList = MIDIPacketList.Builder(byteSize: 1024)
    guard packetList.append(timestamp: 0, data: [0xB0, controller, value]) != nil else {
        throw ProbeFailure.message("could not construct MIDI packet list")
    }
    let status = packetList.withUnsafePointer { pointer in
        MIDISend(port, destination, pointer)
    }
    guard status == noErr else {
        throw ProbeFailure.message("CoreMIDI send failed with status \(status)")
    }
}

guard let arguments = parseArguments() else {
    FileHandle.standardError.write(
        Data(
            "usage: tonex_midi_cc_probe.swift --controller 0..127 --values v,... "
                .appending("--restore-value 0..127 [--delay seconds]\n").utf8
        )
    )
    exit(2)
}

setlinebuf(stdout)

let destinations = matchingDestinations()
guard destinations.count == 1, let (destination, destinationName) = destinations.first else {
    FileHandle.standardError.write(
        Data("error: expected exactly one TONEX MIDI destination; found \(destinations.count)\n".utf8)
    )
    exit(1)
}

var client = MIDIClientRef()
var outputPort = MIDIPortRef()
guard MIDIClientCreate("TONEX MIDI CC probe" as CFString, nil, nil, &client) == noErr,
      MIDIOutputPortCreate(client, "TONEX CC probe output" as CFString, &outputPort) == noErr else {
    FileHandle.standardError.write(Data("error: could not create CoreMIDI output\n".utf8))
    exit(1)
}

var restoreArmed = false
defer {
    if restoreArmed {
        try? sendControlChange(
            controller: arguments.controller,
            value: arguments.restoreValue,
            port: outputPort,
            destination: destination
        )
        Thread.sleep(forTimeInterval: 0.25)
        print("cleanup_restore controller=\(arguments.controller) value=\(arguments.restoreValue)")
    }
    MIDIPortDispose(outputPort)
    MIDIClientDispose(client)
}

do {
    print(
        "destination=\(destinationName) channel=1 controller=\(arguments.controller) "
            + "restore_value=\(arguments.restoreValue) targets=\(arguments.values.count)"
    )
    restoreArmed = true
    for value in arguments.values {
        try sendControlChange(
            controller: arguments.controller,
            value: value,
            port: outputPort,
            destination: destination
        )
        print("sent controller=\(arguments.controller) value=\(value)")
        Thread.sleep(forTimeInterval: arguments.delay)
    }
    try sendControlChange(
        controller: arguments.controller,
        value: arguments.restoreValue,
        port: outputPort,
        destination: destination
    )
    Thread.sleep(forTimeInterval: arguments.delay)
    restoreArmed = false
    print("restored controller=\(arguments.controller) value=\(arguments.restoreValue)")
    print("result=ok targets=\(arguments.values.count)")
} catch {
    FileHandle.standardError.write(Data("error: \(error)\n".utf8))
    exit(1)
}
