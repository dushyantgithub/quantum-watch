import Foundation

/// Decodes IMA-ADPCM audio packets from BLE into 16-bit PCM samples.
/// Packet format: [2-byte LE sequence number][238-byte IMA-ADPCM data]
class AudioDecoder {
    private var pcmBuffer: [Int16] = []
    private var receivedSequences: Set<UInt16> = Set()
    private var packets: [(seq: UInt16, data: Data)] = []

    // IMA-ADPCM state
    private var predictor: Int32 = 0
    private var stepIndex: Int32 = 0

    // IMA-ADPCM step size table
    private static let stepTable: [Int32] = [
        7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
        19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
        50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
        130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
        337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
        876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
        2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
        5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
        15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
    ]

    // Index adjustment table
    private static let indexTable: [Int32] = [
        -1, -1, -1, -1, 2, 4, 6, 8,
        -1, -1, -1, -1, 2, 4, 6, 8
    ]

    func reset() {
        pcmBuffer.removeAll()
        receivedSequences.removeAll()
        packets.removeAll()
        predictor = 0
        stepIndex = 0
    }

    func addPacket(_ data: Data) {
        guard data.count >= 3 else { return } // minimum: 2-byte seq + 1 byte data

        let seqNum = data.withUnsafeBytes { ptr -> UInt16 in
            ptr.load(as: UInt16.self)
        }

        guard !receivedSequences.contains(seqNum) else { return }
        receivedSequences.insert(seqNum)

        let adpcmData = data.subdata(in: 2..<data.count)
        packets.append((seq: seqNum, data: adpcmData))
    }

    func decodeAll() -> Data {
        // Sort packets by sequence number
        packets.sort { $0.seq < $1.seq }

        // Reset ADPCM state
        predictor = 0
        stepIndex = 0
        pcmBuffer.removeAll()

        // Decode each packet
        for packet in packets {
            decodeADPCM(packet.data)
        }

        // Convert Int16 array to Data (little-endian)
        var outputData = Data(capacity: pcmBuffer.count * 2)
        for sample in pcmBuffer {
            var le = sample.littleEndian
            outputData.append(Data(bytes: &le, count: 2))
        }

        return outputData
    }

    private func decodeADPCM(_ data: Data) {
        for byte in data {
            // Each byte contains two 4-bit ADPCM nibbles (low nibble first)
            let lowNibble = Int32(byte & 0x0F)
            let highNibble = Int32((byte >> 4) & 0x0F)

            pcmBuffer.append(decodeNibble(lowNibble))
            pcmBuffer.append(decodeNibble(highNibble))
        }
    }

    private func decodeNibble(_ nibble: Int32) -> Int16 {
        let step = AudioDecoder.stepTable[Int(stepIndex)]

        var diff = step >> 3
        if nibble & 4 != 0 { diff += step }
        if nibble & 2 != 0 { diff += step >> 1 }
        if nibble & 1 != 0 { diff += step >> 2 }

        if nibble & 8 != 0 {
            predictor -= diff
        } else {
            predictor += diff
        }

        // Clamp to Int16 range
        predictor = max(-32768, min(32767, predictor))

        // Update step index
        stepIndex += AudioDecoder.indexTable[Int(nibble)]
        stepIndex = max(0, min(88, stepIndex))

        return Int16(predictor)
    }

    var packetCount: Int { packets.count }
    var hasData: Bool { !packets.isEmpty }
}
