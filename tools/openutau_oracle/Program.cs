using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using OpenUtau.Core;
using OpenUtau.Core.Ustx;
using OpenUtau.Core.Format;
using Melanchall.DryWetMidi.Core;
using Melanchall.DryWetMidi.Interaction;

// External oracle for SEAM's score export.
//
// Nothing in SEAM participates in this program. It reads a SEAM-produced file with
// OpenUtau's own deserializer and model types (USTX) and with DryWetMidi, the library
// OpenUtau uses to read MIDI. Agreement here is evidence about the file, not a
// self-consistency check of SEAM's reader and writer.
//
// Build and run instructions are in README.md next to this file. This program is
// deliberately not part of the repository's build or CI: OpenUtau and the .NET SDK are
// not dependencies of SEAM, and an optional oracle that silently stops running would be
// worse than one that is honestly absent.
internal static class Program {
    private static int Main(string[] args) {
        if (args.Length == 2 && args[0] == "--emit-fixture") return EmitFixture(args[1], false, false);
        if (args.Length == 2 && args[0] == "--emit-curve-fixture") return EmitFixture(args[1], true, false);
        if (args.Length == 2 && args[0] == "--emit-multiline-fixture") return EmitFixture(args[1], false, true);
        if (args.Length == 3 && args[0] == "--compare-pitch") return ComparePitch(args[1], args[2]);
        if (args.Length >= 2 && args[1] == "--midi") return ReadMidi(args[0]);
        if (args.Length < 1) {
            Console.Error.WriteLine("usage: seam_ustx_oracle FILE.ustx");
            Console.Error.WriteLine("       seam_ustx_oracle FILE.mid --midi");
            Console.Error.WriteLine("       seam_ustx_oracle --emit-fixture NEW_FILE.ustx");
            Console.Error.WriteLine("       seam_ustx_oracle --emit-curve-fixture NEW_FILE.ustx");
            Console.Error.WriteLine("       seam_ustx_oracle --emit-multiline-fixture NEW_FILE.ustx");
            Console.Error.WriteLine("       seam_ustx_oracle --compare-pitch SOURCE.ustx ROUNDTRIP.ustx");
            return 2;
        }
        return ReadUstx(args[0]);
    }

    // Compare authored score pitch using OpenUtau's own note pitch sampler,
    // not SEAM's importer or a textual equality check. Vibrato is a separate
    // modulation and is deliberately excluded from this contour comparison.
    private static int ComparePitch(string sourcePath, string roundTripPath) {
        try {
            var tuningField = ResolveTuningField();
            var source = Ustx.Load(sourcePath);
            var roundTrip = Ustx.Load(roundTripPath);
            var sourceParts = source.parts.OfType<UVoicePart>().ToList();
            var roundTripParts = roundTrip.parts.OfType<UVoicePart>().ToList();
            if (sourceParts.Count != 1 || roundTripParts.Count != 1)
                throw new InvalidDataException("Expected one voice part in both scores");
            var sourceNotes = sourceParts[0].notes.ToList();
            var roundTripNotes = roundTripParts[0].notes.ToList();
            if (sourceNotes.Count != roundTripNotes.Count)
                throw new InvalidDataException("Note count changed across exchange");
            double maximumError = 0;
            int samples = 0;
            for (int index = 0; index < sourceNotes.Count; index++) {
                var original = sourceNotes[index];
                var exported = roundTripNotes[index];
                if (original.position != exported.position || original.duration != exported.duration ||
                    original.tone != exported.tone || original.lyric != exported.lyric)
                    throw new InvalidDataException("Note score changed at index " + index);
                // UPitch.Sample returns null exactly on a singleton point's X;
                // sample one tick inside onset instead of mistaking that API
                // edge case for a missing musical pitch.
                foreach (int offset in new[] { 1, 60, 120, 180, 240, 300, 360, 420, 479 }) {
                    if (offset >= original.duration) continue;
                    var tick = original.position + offset;
                    var originalOffset = original.pitch.Sample(source, sourceParts[0], original, tick);
                    var exportedOffset = exported.pitch.Sample(roundTrip, roundTripParts[0], exported, tick);
                    if (!originalOffset.HasValue || !exportedOffset.HasValue)
                        throw new InvalidDataException("OpenUtau could not sample note pitch");
                    var originalTuning = Convert.ToDouble(tuningField?.GetValue(original) ?? 0);
                    var exportedTuning = Convert.ToDouble(tuningField?.GetValue(exported) ?? 0);
                    var originalCents = original.tone * 100.0 + originalTuning + originalOffset.Value;
                    var exportedCents = exported.tone * 100.0 + exportedTuning + exportedOffset.Value;
                    if (!double.IsFinite(originalCents) || !double.IsFinite(exportedCents))
                        throw new InvalidDataException("OpenUtau returned non-finite note pitch");
                    maximumError = Math.Max(maximumError, Math.Abs(originalCents - exportedCents));
                    samples++;
                }
            }
            if (samples == 0)
                throw new InvalidDataException("No in-note pitch samples were available");
            Console.WriteLine("pitchSamples=" + samples);
            Console.WriteLine("maxPitchCentsError=" + maximumError.ToString("F6", System.Globalization.CultureInfo.InvariantCulture));
            if (maximumError > 0.5) {
                Console.Error.WriteLine("PITCH_COMPARE_FAIL: exceeds 0.5 cent on the declared sample grid");
                return 1;
            }
            Console.WriteLine("PITCH_COMPARE_OK");
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine("PITCH_COMPARE_FAILED: " + error);
            return 1;
        }
    }

    // Build against a specific historical OpenUtau.Core checkout, then use
    // that version's own project model and serializer to write a bounded
    // interoperability fixture. This does not impersonate a GUI Save action.
    private static System.Reflection.FieldInfo ResolveTuningField() {
        var field = typeof(UNote).GetField("tuning");
        if (field == null && Ustx.kUstxVersion.CompareTo(new Version(0, 8)) >= 0)
            throw new InvalidDataException("Expected UNote.tuning in OpenUtau USTX " + Ustx.kUstxVersion);
        return field;
    }

    private static int EmitFixture(string path, bool withCurve, bool withMultilineComment) {
        try {
            var tuningField = ResolveTuningField();
            var project = Ustx.Create();
            project.name = "SEAM historical serializer interop";
            if (withMultilineComment) project.comment = "First # & * !\nSecond line";
            project.timeSignatures = new List<UTimeSignature> {
                new UTimeSignature(0, 4, 4), new UTimeSignature(2, 3, 4) };
            project.tempos = new List<UTempo> {
                new UTempo(0, 120), new UTempo(480, 150) };
            project.tracks[0].TrackName = "Lead";
            project.tracks[0].Volume = -3;
            project.tracks[0].Pan = 0.25;

            var part = new UVoicePart { name = "Verse", trackNo = 0,
                position = 960, duration = 960 };
            var first = UNote.Create();
            first.position = 0; first.duration = 480; first.tone = 60;
            first.lyric = "あ";
            // `tuning` was introduced after USTX 0.7; leave old model types
            // untouched, but exercise it when the historical assembly has it.
            tuningField?.SetValue(first, 25);
            first.pitch.AddPoint(new PitchPoint(0, 0));
            first.pitch.AddPoint(new PitchPoint(250, 5));
            first.vibrato.length = 60;
            part.notes.Add(first);
            var second = UNote.Create();
            second.position = 480; second.duration = 480; second.tone = 62;
            second.lyric = "い";
            second.pitch.AddPoint(new PitchPoint(0, 0));
            part.notes.Add(second);
            if (withCurve) {
                var curve = new UCurve(project.expressions["dyn"]);
                for (int index = 0; index < 64; index++) {
                    curve.xs.Add(index * UCurve.interval);
                    curve.ys.Add(60 + index % 20);
                }
                part.curves.Add(curve);
            }
            project.parts.Add(part);

            project.ustxVersion = Ustx.kUstxVersion;
            project.ValidateFull();
            project.BeforeSave();
            var serialized = Yaml.DefaultSerializer.Serialize(project);
            using (var output = new FileStream(path, FileMode.CreateNew, FileAccess.Write)) {
                using var writer = new StreamWriter(output, System.Text.Encoding.UTF8);
                writer.Write(serialized);
            }
            project.AfterSave();
            Console.WriteLine("EMITTED_USTX_VERSION=" + Ustx.kUstxVersion);
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine("EMIT_FAILED: " + error.GetType().Name + ": " + error.Message);
            return 1;
        }
    }

    // Reads the SMF half of the exchange with the library an OpenUtau-family reader uses.
    private static int ReadMidi(string path) {
        try {
            Console.OutputEncoding = System.Text.Encoding.UTF8;
        } catch (IOException) {
            // A redirected console may refuse an encoding change; the hex output stays authoritative.
        }
        try {
            // DryWetMidi decodes text meta events with ASCII by default, which reports a correct
            // UTF-8 payload as "???". Read with UTF-8 so the oracle measures the file, not its own default.
            var midi = MidiFile.Read(path, new ReadingSettings { TextEncoding = System.Text.Encoding.UTF8 });
            Console.WriteLine("midiFormat=" + midi.OriginalFormat);
            Console.WriteLine("midiTracks=" + midi.GetTrackChunks().Count());
            Console.WriteLine("midiTimeDivision=" + midi.GetTempoMap().TimeDivision);
            var notes = midi.GetNotes().ToList();
            Console.WriteLine("midiNotes=" + notes.Count);
            foreach (var note in notes)
                Console.WriteLine("midiNote=" + note.Time + "," + note.Length + "," + note.NoteNumber + "," + note.Velocity);
            // SEAM writes lyric meta events (0xFF 0x05). Query lyric and plain text events so the
            // oracle reports what is in the file rather than what one query expects to find.
            var textEvents = midi.GetTimedEvents().Where(e => e.Event is TextEvent || e.Event is LyricEvent).ToList();
            Console.WriteLine("midiTextEvents=" + textEvents.Count);
            foreach (var item in textEvents) {
                var kind = item.Event is LyricEvent ? "lyric:" : "text:";
                var payload = item.Event is LyricEvent lyric ? lyric.Text : ((TextEvent)item.Event).Text;
                Console.WriteLine("midiText=" + item.Time + "," + kind + payload);
                Console.WriteLine("midiTextHex=" + item.Time + ","
                    + string.Join(" ", System.Text.Encoding.UTF8.GetBytes(payload).Select(b => b.ToString("x2"))));
            }
            Console.WriteLine("ORACLE_MIDI_OK");
            return 0;
        } catch (Exception error) {
            Console.Error.WriteLine("MIDI_READ_FAILED: " + error.GetType().Name + ": " + error.Message);
            return 1;
        }
    }

    // Use the actual OpenUtau load path, including migration, AfterLoad and validation.
    // A bare YAML deserialization cannot establish that the editor accepts a project.
    private static int ReadUstx(string path) {
        UProject project;
        System.Reflection.FieldInfo tuningField;
        try {
            project = Ustx.Load(path);
            tuningField = ResolveTuningField();
        } catch (Exception error) {
            Console.Error.WriteLine("LOAD_FAILED: " + error);
            return 1;
        }

        Console.WriteLine("ustxVersion=" + project.ustxVersion);
        Console.WriteLine("name=" + project.name);
        Console.WriteLine("tempos=" + project.tempos.Count);
        Console.WriteLine("timeSignatures=" + project.timeSignatures.Count);
        Console.WriteLine("tracks=" + project.tracks.Count);
        if (project.tracks.Count > 0) {
            Console.WriteLine("track0.name=" + project.tracks[0].TrackName);
            Console.WriteLine("track0.singer=" + (project.tracks[0].singer ?? ""));
        }
        var parts = project.parts.OfType<UVoicePart>().ToList();
        Console.WriteLine("voiceParts=" + parts.Count);
        if (parts.Count > 0) {
            var part = parts[0];
            Console.WriteLine("part0.name=" + part.name);
            Console.WriteLine("part0.trackNo=" + part.trackNo);
            Console.WriteLine("part0.position=" + part.position);
            Console.WriteLine("part0.duration=" + part.duration);
            Console.WriteLine("part0.notes=" + part.notes.Count);
            foreach (var note in part.notes) {
                var tuning = tuningField?.GetValue(note)?.ToString() ?? "absent";
                Console.WriteLine("note=" + note.position + "," + note.duration + "," + note.tone + "," + note.lyric
                    + ",tuning=" + tuning
                    + ",pitchPoints=" + (note.pitch?.data?.Count ?? 0)
                    + ",vibratoLength=" + (note.vibrato?.length ?? 0));
            }
        }
        project.BeforeSave();
        var roundTripped = Yaml.DefaultDeserializer.Deserialize<UProject>(Yaml.DefaultSerializer.Serialize(project));
        project.AfterSave();
        Console.WriteLine("roundTripTempos=" + roundTripped.tempos.Count);
        Console.WriteLine("ORACLE_OK");
        return 0;
    }
}
