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
        if (args.Length >= 2 && args[1] == "--midi") return ReadMidi(args[0]);
        if (args.Length < 1) {
            Console.Error.WriteLine("usage: seam_ustx_oracle FILE.ustx");
            Console.Error.WriteLine("       seam_ustx_oracle FILE.mid --midi");
            return 2;
        }
        return ReadUstx(args[0]);
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
        try {
            project = Ustx.Load(path);
        } catch (Exception error) {
            Console.Error.WriteLine("LOAD_FAILED: " + error.GetType().Name + ": " + error.Message);
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
                Console.WriteLine("note=" + note.position + "," + note.duration + "," + note.tone + "," + note.lyric
                    + ",tuning=" + note.tuning
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
