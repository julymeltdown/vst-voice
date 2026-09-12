# Original singer pilot 01

Development-only inventory profile. This is not a qualified singer, range,
recipe, recording, model or release resource. No audio or approval is implied.

Generate the proposed Japanese coverage, across three planned pitch layers:

```sh
python3 tools/voicebank-script-generator/main.py --draft --profile assets/pilots/seam-pilot-01/profile.json
```

The explicit draft path emits schema 2, style-owned assignments and a
`NOT_ASSESSED` range assessment. It does not inherit the legacy profile's PASS.
`--json-output`, `--csv-output` and `--production-assignments-output` select output
files; choose new destinations to preserve prior artifacts. The assignment
document declares its requirement for the planned schema-4 producer writer.
Legacy producer readers reject this new inventory rather than dropping styles.

Next implementation work connects the schema-4 producer and generation planner,
then measures articulation and the actual voice before generating a full bank.
