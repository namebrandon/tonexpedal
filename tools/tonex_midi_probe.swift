#!/usr/bin/env swift

import CoreMIDI
import Darwin
import Foundation

struct ProbeArguments {
    let restoreIndex: Int
    let delay: TimeInterval
    let targets: [Int]
}

enum ProbeFailure: Error, CustomStringConvertible {
    case message(String)

    var description: String {
        switch self {
        case .message(let text): return text
        }
    }
}

func parseArguments() -> ProbeArguments? {
    let arguments = Array(CommandLine.arguments.dropFirst())
    var restoreIndex: Int?
    var delay: TimeInterval = 1.5
    var targets = [0, 127, 128, 149]
    var offset = 0

    while offset < arguments.count {
        guard offset + 1 < arguments.count else { return nil }
        let option = arguments[offset]
        let value = arguments[offset + 1]
        switch option {
        case "--restore-index":
            guard restoreIndex == nil,
                  let parsedIndex = Int(value),
                  (0..<150).contains(parsedIndex) else {
                return nil
            }
            restoreIndex = parsedIndex
        case "--delay":
            guard let parsedDelay = TimeInterval(value), parsedDelay >= 0.05 else {
                return nil
            }
            delay = parsedDelay
        case "--indices":
            let parsedTargets = value.split(separator: ",").compactMap { Int($0) }
            guard !parsedTargets.isEmpty,
                  parsedTargets.count == value.split(separator: ",").count,
                  parsedTargets.allSatisfy({ (0..<150).contains($0) }) else {
                return nil
            }
            targets = parsedTargets
        default:
            return nil
        }
        offset += 2
    }

    guard let restoreIndex else { return nil }
    return ProbeArguments(restoreIndex: restoreIndex, delay: delay, targets: targets)
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

func displayLabel(for index: Int) -> String {
    let slot = ["A", "B", "C"][index % 3]
    return "\(index / 3).\(slot)"
}

func midiMessages(for index: Int) -> ([UInt8], [UInt8]) {
    let bank = UInt8(index / 128)
    let program = UInt8(index % 128)
    return ([0xB0, 0x00, bank], [0xC0, program])
}

func sendPreset(_ index: Int, port: MIDIPortRef, destination: MIDIEndpointRef) throws {
    let messages = midiMessages(for: index)
    // MIDIPacket contains a 256-byte inline data area even for short messages.
    let packetList = MIDIPacketList.Builder(byteSize: 1024)
    guard packetList.append(timestamp: 0, data: messages.0) != nil,
          packetList.append(timestamp: 0, data: messages.1) != nil else {
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
        Data("usage: tonex_midi_probe.swift --restore-index 0..149 [--delay seconds] [--indices i,j,...]\n".utf8)
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
guard MIDIClientCreate("TONEX MIDI probe" as CFString, nil, nil, &client) == noErr,
      MIDIOutputPortCreate(client, "TONEX probe output" as CFString, &outputPort) == noErr else {
    FileHandle.standardError.write(Data("error: could not create CoreMIDI output\n".utf8))
    exit(1)
}

var restoreArmed = false
defer {
    if restoreArmed {
        try? sendPreset(arguments.restoreIndex, port: outputPort, destination: destination)
        Thread.sleep(forTimeInterval: 0.25)
        print("cleanup_restore index=\(arguments.restoreIndex) display=\(displayLabel(for: arguments.restoreIndex))")
    }
    MIDIPortDispose(outputPort)
    MIDIClientDispose(client)
}

do {
    print("destination=\(destinationName) channel=1 restore_index=\(arguments.restoreIndex) targets=\(arguments.targets.count)")
    restoreArmed = true
    for index in arguments.targets {
        try sendPreset(index, port: outputPort, destination: destination)
        print("sent index=\(index) display=\(displayLabel(for: index)) bank_select=\(index / 128) program=\(index % 128)")
        Thread.sleep(forTimeInterval: arguments.delay)
    }
    try sendPreset(arguments.restoreIndex, port: outputPort, destination: destination)
    Thread.sleep(forTimeInterval: arguments.delay)
    restoreArmed = false
    print("restored index=\(arguments.restoreIndex) display=\(displayLabel(for: arguments.restoreIndex))")
    print("result=ok targets=\(arguments.targets.count)")
} catch {
    FileHandle.standardError.write(Data("error: \(error)\n".utf8))
    exit(1)
}
