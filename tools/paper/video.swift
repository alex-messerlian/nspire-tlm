// video.swift -- the only code that touches the calculator video. macOS AVFoundation, nothing to install.
//
//   swift tools/paper/video.swift probe     <in>                    duration, tracks, every metadata item
//   swift tools/paper/video.swift frame     <in> <out.png> <t>      one frame at t seconds, upright, full size
//   swift tools/paper/video.swift transcode <in> <out.mp4> [t0 t1]  H.264, 1080x1920, 30 fps, no audio,
//                                                                    NO METADATA; t0..t1 cuts one continuous span
//
// WHY TRANSCODE AT ALL. The phone's file is 2.07 GB of 4K HEVC at 60 fps, and it carries the place it
// was filmed (com.apple.quicktime.location.ISO6709) and the phone model. TMLR's supplementary material
// is anonymous and at most 100 MB. The writer below copies no metadata item, no timed-metadata track
// and no audio, so the output carries only pictures and their times; `probe` on the output shows it.
// Frame times are kept: nothing is sped up, slowed down or reordered, and a span is one cut at each
// end, never inside.
import AVFoundation
import AppKit

let args = CommandLine.arguments
func die(_ s: String) -> Never { FileHandle.standardError.write((s + "\n").data(using: .utf8)!); exit(2) }
guard args.count >= 3 else { die("usage: video.swift probe|frame|transcode <in> ...") }
let asset = AVURLAsset(url: URL(fileURLWithPath: args[2]))

func run(_ body: @escaping () async throws -> Void) {
    let done = DispatchSemaphore(value: 0)
    Task { do { try await body() } catch { die("error: \(error)") }; done.signal() }
    done.wait()
}

switch args[1] {
case "probe":
    run {
        print("duration_s", CMTimeGetSeconds(try await asset.load(.duration)))
        for t in try await asset.load(.tracks) {
            let size = try await t.load(.naturalSize), fps = try await t.load(.nominalFrameRate)
            let rate = try await t.load(.estimatedDataRate)
            print("track", t.mediaType.rawValue, "size", Int(size.width), Int(size.height), "fps", fps,
                  "Mbps", String(format: "%.2f", rate / 1e6))
        }
        var n = 0
        for fmt in try await asset.load(.availableMetadataFormats) {
            for item in try await asset.loadMetadata(for: fmt) {
                let v: Any? = (try? await item.load(.value)) ?? nil
                print("meta", item.identifier?.rawValue ?? "?", String(describing: v).prefix(100)); n += 1
            }
        }
        print("metadata_items", n)
    }

case "frame":
    guard args.count == 5, let t = Double(args[4]) else { die("usage: video.swift frame <in> <out.png> <t>") }
    let gen = AVAssetImageGenerator(asset: asset)
    gen.appliesPreferredTrackTransform = true
    gen.requestedTimeToleranceBefore = .zero; gen.requestedTimeToleranceAfter = .zero
    run {
        let (im, actual) = try await gen.image(at: CMTime(seconds: t, preferredTimescale: 6000))
        let png = NSBitmapImageRep(cgImage: im).representation(using: .png, properties: [:])!
        try png.write(to: URL(fileURLWithPath: args[3]))
        print("frame", String(format: "%.4f", CMTimeGetSeconds(actual)), im.width, im.height)
    }

case "transcode":
    guard args.count == 4 || args.count == 6 else { die("usage: video.swift transcode <in> <out.mp4> [t0 t1]") }
    let out = URL(fileURLWithPath: args[3])
    try? FileManager.default.removeItem(at: out)
    run {
        guard let src = try await asset.loadTracks(withMediaType: .video).first else { die("no video track") }
        let natural = try await src.load(.naturalSize), xf = try await src.load(.preferredTransform)
        let full = CMTimeRange(start: .zero, duration: try await asset.load(.duration))
        let span = args.count == 6
            ? CMTimeRange(start: CMTime(seconds: Double(args[4])!, preferredTimescale: 6000),
                          end: CMTime(seconds: Double(args[5])!, preferredTimescale: 6000))
            : full
        // Upright and half size: rotate by the phone's own transform, then scale 3840x2160 -> 1080x1920.
        let upright = CGRect(origin: .zero, size: natural).applying(xf).size
        let scale = 1080.0 / min(abs(upright.width), abs(upright.height))
        let W = Int((abs(upright.width) * scale).rounded()), H = Int((abs(upright.height) * scale).rounded())
        let layer = AVMutableVideoCompositionLayerInstruction(assetTrack: src)
        layer.setTransform(xf.concatenating(CGAffineTransform(scaleX: scale, y: scale)), at: .zero)
        let ins = AVMutableVideoCompositionInstruction()
        ins.timeRange = full; ins.layerInstructions = [layer]
        let comp = AVMutableVideoComposition()
        comp.renderSize = CGSize(width: W, height: H)
        comp.frameDuration = CMTime(value: 1, timescale: 30)
        comp.instructions = [ins]

        let reader = try AVAssetReader(asset: asset)
        reader.timeRange = span
        let rout = AVAssetReaderVideoCompositionOutput(videoTracks: [src], videoSettings:
            [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange])
        rout.videoComposition = comp
        reader.add(rout)

        let writer = try AVAssetWriter(outputURL: out, fileType: .mp4)
        writer.metadata = []                       // nothing from the phone: no place, no model, no date
        writer.shouldOptimizeForNetworkUse = true
        let win = AVAssetWriterInput(mediaType: .video, outputSettings: [
            AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: W, AVVideoHeightKey: H,
            AVVideoCompressionPropertiesKey: [
                AVVideoAverageBitRateKey: 2_500_000,
                AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel,
                AVVideoMaxKeyFrameIntervalKey: 60,
                AVVideoExpectedSourceFrameRateKey: 30]])
        win.expectsMediaDataInRealTime = false
        writer.add(win)
        guard reader.startReading() else { die("reader: \(String(describing: reader.error))") }
        guard writer.startWriting() else { die("writer: \(String(describing: writer.error))") }
        writer.startSession(atSourceTime: .zero)
        var frames = 0
        while let sb = rout.copyNextSampleBuffer() {
            // A span starts at zero in the output; times inside it are unchanged.
            var timing = CMSampleTimingInfo()
            CMSampleBufferGetSampleTimingInfo(sb, at: 0, timingInfoOut: &timing)
            timing.presentationTimeStamp = CMTimeSubtract(timing.presentationTimeStamp, span.start)
            timing.decodeTimeStamp = .invalid
            var moved: CMSampleBuffer?
            CMSampleBufferCreateCopyWithNewTiming(allocator: nil, sampleBuffer: sb, sampleTimingEntryCount: 1,
                                                  sampleTimingArray: &timing, sampleBufferOut: &moved)
            while !win.isReadyForMoreMediaData { usleep(2000) }
            if let m = moved, win.append(m) { frames += 1 } else { die("append failed: \(String(describing: writer.error))") }
        }
        if reader.status == .failed { die("reader: \(String(describing: reader.error))") }
        win.markAsFinished()
        await writer.finishWriting()
        if writer.status != .completed { die("writer: \(String(describing: writer.error))") }
        print("wrote", out.path, W, H, "frames", frames,
              "span_s", String(format: "%.3f..%.3f", CMTimeGetSeconds(span.start), CMTimeGetSeconds(span.end)))
    }

default:
    die("unknown command \(args[1])")
}
