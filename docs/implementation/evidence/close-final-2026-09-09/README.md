# Repair execution evidence

`ctest-full.log` is the command output for the full 120-entry run.
`ctest-cases.log` retains individual command/output records, including 818 core
and 20 Studio draft/close cases. `identity.json` binds the intentionally changed
source files and the executed plugin. These are engineering checks, not a
signed-installed product qualification. The sole full-run failure is unindexed
required inputs; this review does not modify the Git index.
