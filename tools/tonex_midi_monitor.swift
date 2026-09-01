#!/usr/bin/env swift

import CoreMIDI
import Foundation

func endpointName(_ endpoint: MIDIEndpointRef) -> String {
    var value: Unmanaged<CFString>?
    guard MIDIObjectGetStringProperty(endpoint, kMIDIPropertyDisplayName, &value) == noErr,
          let value else {
        return "Unnamed MIDI endpoint"
    }
    return value.takeRetainedValue() as String
}

func durationFromArguments() -> TimeInterval? {
    let arguments = Array(CommandLine.arguments.dropFirst())
    if arguments.isEmpty {
        return 5
    }
    guard arguments.count == 2,
          arguments[0] == "--seconds",
          let duration = TimeInterval(arguments[1]),
          duration > 0 else {
        return nil
    }
    return duration
}

guard let duration = durationFromArguments() else {
    FileHandle.standardError.write(
        Data("usage: tonex_midi_monitor.swift [--seconds N]\n".utf8)
    )
    exit(2)
}

setlinebuf(stdout)

var client = MIDIClientRef()
var inputPort = MIDIPortRef()
var messageCount = 0
let messageCountLock = NSLock()

guard MIDIClientCreate("TONEX read-only monitor" as CFString, nil, nil, &client) == noErr else {
    FileHandle.standardError.write(Data("error: could not create CoreMIDI client\n".utf8))
    exit(1)
}

defer {
    MIDIPortDispose(inputPort)
    MIDIClientDispose(client)
}

let portStatus = MIDIInputPortCreateWithBlock(
    client,
    "TONEX read-only input" as CFString,
    &inputPort
) { packetList, _ in
    for packet in packetList.unsafeSequence() {
        let bytes = Array(packet.bytes())
        messageCountLock.lock()
        messageCount += 1
        messageCountLock.unlock()
        print("message bytes=\(bytes.map { String(format: "%02x", $0) }.joined(separator: " "))")
    }
}

guard portStatus == noErr else {
    FileHandle.standardError.write(Data("error: could not create CoreMIDI input port\n".utf8))
    exit(1)
}

var connectedSources = [String]()
for index in 0..<MIDIGetNumberOfSources() {
    let source = MIDIGetSource(index)
    let name = endpointName(source)
    guard name.localizedCaseInsensitiveContains("tonex") else {
        continue
    }
    guard MIDIPortConnectSource(inputPort, source, nil) == noErr else {
        FileHandle.standardError.write(Data("warning: could not connect source \(name)\n".utf8))
        continue
    }
    connectedSources.append(name)
}

guard !connectedSources.isEmpty else {
    FileHandle.standardError.write(Data("error: no TONEX MIDI source found\n".utf8))
    exit(1)
}

print("read_only=true sources=\(connectedSources.joined(separator: ", ")) seconds=\(duration)")
RunLoop.current.run(until: Date(timeIntervalSinceNow: duration))
messageCountLock.lock()
let finalMessageCount = messageCount
messageCountLock.unlock()
print("result=ok messages=\(finalMessageCount)")
