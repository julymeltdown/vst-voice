// Repackage this audit's Markdown and preserve current test evidence.
// This generates report artifacts only; it does not edit product sources.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';

const output = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(output, '../../..');
const reportPath = 'DEVELOPMENT_DIRECTION_POST_INTEGRATION_REVIEW_2026-09-09_KO.md';
const prefix = 'docs/reviews/direction-post-integration-2026-09-09/';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = p => fs.readFileSync(path.join(root, p));
const expectedDiff = '9487c857832850d7d190a16897de60ae49ef6a7e4795451b78ae2e829edacc1b';
const diff = execFileSync('git', ['diff', '--binary'], { cwd: root, maxBuffer: 20 * 1024 * 1024 });
if (sha(diff) !== expectedDiff) throw new Error('Tracked source changed after the reviewed snapshot. Do not re-label the evidence.');
const capturedAt = new Date().toISOString();
const log = read('build/release/Testing/Temporary/LastTest.log');
const failures = read('build/release/Testing/Temporary/LastTestsFailed.log').toString().trim().split('\n').map(x => x.slice(x.indexOf(':') + 1));
const testNames = [...log.toString().matchAll(/^\d+\/116 Testing: (.+)$/gm)].map(x => x[1]);
const caseFailures = [...new Set([...log.toString().matchAll(/^\[FAIL\] (.+)$/gm)].map(x => x[1]))];
if (testNames.length !== 116 || failures.length !== 6 || caseFailures.length !== 4) throw new Error('CTest evidence is not the reviewed run.');
if (!log.includes('769 passed, 4 failed')) throw new Error('Core result differs from reviewed run.');
const copyEvidence = (from, to) => fs.copyFileSync(path.join(root, from), path.join(output, to));
copyEvidence('build/release/Testing/Temporary/LastTest.log', 'ctest-full.log');
copyEvidence('build/release/Testing/Temporary/LastTestsFailed.log', 'ctest-failures.txt');
copyEvidence('build/release/phase12c/matrix.json', 'matrix.json');
const contract = JSON.parse(read('docs/product/full-product-beta-contract.json'));
const matrix = JSON.parse(read('build/release/phase12c/matrix.json'));
const criteria = contract.evaluationProfile.criteria.reduce((counts, row) => {
  counts[row.status] = (counts[row.status] || 0) + 1; return counts;
}, {});
const evidencePaths = [reportPath, 'docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md',
  'docs/product/full-product-beta-contract.json', 'build/release/libseam_voicebank_production.a',
  'build/release/libseam_phonemizer.a', prefix + 'recovery_review_probe.cpp',
  prefix + 'language_path_probe.cpp', prefix + 'gate_subpredicate_probe.py'];
const results = {
  reviewDate: '2026-09-09', timezone: 'Asia/Seoul', capturedAt,
  branch: 'codex/production-readiness-completion', head: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root }).toString().trim(),
  scope: 'Read-only product-source review; report and isolated synthetic diagnostics only. No product/test edits, staging, commits, pushes, reviews, or release promotions.',
  verdict: { architecture: 'RETAIN', execution: 'REFOCUS_ON_COMPLETE_CREATOR_SINGER_SONG_PATH', integration: 'NOT_ACCEPTED', fullScopeBeta: 'NO_GO' },
  worktreeBeforeArtifacts: { trackedChanged: 208, untrackedFiles: 314, totalPaths: 522, trackedInsertions: 22629, trackedDeletions: 1184, untrackedLinesExcluded: true, trackedDiffSha256: expectedDiff },
  build: { command: 'cmake --build build/release -j 4', exitCode: 0, output: 'ninja: no work to do.', cleanCheckout: false },
  ctest: { command: 'ctest --test-dir build/release --output-on-failure -j 1 --output-log direction-review-2026-09-09.log',
    durableLog: prefix + 'ctest-full.log', logSha256: sha(log), startKst: '2026-09-09 03:02', endKst: '2026-09-09 03:06',
    durationSeconds: 248.08, exitCode: 8, total: testNames.length, passed: testNames.length - failures.length, failed: failures.length,
    passPercent: (testNames.length - failures.length) / testNames.length * 100,
    failedSuites: failures, uniqueCppFailedCases: caseFailures, core: { passed: 769, failed: 4 },
    unindexedRequiredInputs: 311, countsOverlap: true,
    focused: { ownership: [8, 0], staging: [12, 0], importOutcomes: [8, 0], sampleReviewCli: [3, 0], studioReview: [8, 1], producer: [32, 2], language: [15, 0] } },
  matrix: { cases: matrix.cases, expected: matrix.expected, failures: matrix.failures,
    executionPath: matrix.executionPath, evidenceScope: matrix.evidenceScope, releaseEligible: matrix.releaseEligible,
    resourceMode: matrix.resourceMode, pluginSha256: matrix.pluginSha256, log: prefix + 'matrix.json' },
  contract: { requirements: contract.requirements.length, cases: contract.cases.length,
    hostTuples: contract.scope.hostTuples.length, matrixStatus: contract.scope.matrixStatus,
    releasedResources: contract.scope.releasedResources.length, evaluationProfileStatus: contract.evaluationProfile.status, criteria },
  ledger: { acceptedUnits: ['U1', 'U2', 'U3', 'U4', 'U5'], totalUnits: 48,
    percent: 5 / 48 * 100, statusIsLedgerHistoryNotFreshReacceptance: true, isProductCompletionPercent: false },
  diagnostics: {
    recovery: { reproduced: true, scope: 'Synthetic isolated fixture, no approval/publication',
      control: { saveGeneration: 2, importGeneration: 3, prepareReview: 'PASS' },
      tornJournal: { gap: 2, recoveredGeneration: 1, saveGeneration: 3, saveVerify: 'PASS', importGeneration: 4, importVerify: 'PASS', prepareReview: 'FAIL',
        error: 'Candidate original import attribution requires contiguous verified generation history', context: 'Missing generation 2' } },
    english: { input: 'sing', tokens: [{ phone: 's', role: 'Onset', voiced: false }, { phone: 'ih1', role: 'Nucleus', voiced: true }, { phone: 'ng', role: 'Onset', voiced: true }], warnings: 0, audibleImpactMeasured: false },
    studioPath: { lexicallyEqual: false, filesystemEquivalent: true, performedByIndependentReviewer: true },
    gate: { case: 'R13.macos-arm64-reaper-clap', readmeAsOperations: true, errors: [], fullReportOrGoBypass: false,
      readmeSha256: '3b12c78feed58e579a90870c20c94f18d42de88d31d56665cfbd11e07fd418e4' },
    tempo: { kind: 'formula counterexample, not native host execution', trueSeconds: 6, mappedSeconds: 8, errorFramesAt48000: 96000 } },
  hashes: Object.fromEntries(evidencePaths.map(p => [p, sha(read(p))])),
  notRun: ['Fresh clean-checkout build', 'Full current GUI visual/input review', 'Windows runtime', 'Actual nine installed DAW tuples', 'Long qualification soak', 'Qualified female singer listening', 'Actual trained neural singing', 'Independent creator/music/language acceptance']
};
fs.writeFileSync(path.join(output, 'results.json'), JSON.stringify(results, null, 2) + '\n');

const report = read(reportPath).toString();
const title = report.split('\n')[0].replace(/^# /, '');
const sections = report.split(/(?=^## )/m);
const failureDefinitions = [
  { id: 'T01', label: 'T01 저장 후 무효 참조', caseName: 'draft import requires actual source execution permission and unchanged evidence', location: 'tests/test_production_draft.cpp:159', suites: ['seam_tests', 'seam_production_draft_tests', 'seam_voicebank_production_tests'] },
  { id: 'T02', label: 'T02 잘못된 중복 키 입력', caseName: 'sample review packet roundtrips bounded material and rejects tampering', location: 'tests/test_voicebank_production_project.cpp:180', suites: ['seam_tests', 'seam_voicebank_production_tests'] },
  { id: 'T03', label: 'T03 이전 오류 계약', caseName: 'articulated candidates bake and enter production with typed unapproved gestures', location: 'tests/test_export_service.cpp:210', suites: ['seam_export_tests', 'seam_tests'] },
  { id: 'T04', label: 'T04 경로 문자열 비교', caseName: 'Studio explicit acceptance publishes an actual new candidate and preserves producer input', location: 'tests/test_studio_sample_review.cpp:186', suites: ['seam_tests', 'seam_studio_sample_review_tests'] },
];
const database = new DatabaseSync(':memory:');
database.exec('CREATE TABLE failed_case_events(case_name TEXT NOT NULL); CREATE TABLE failure_case_definitions(id TEXT, label TEXT, case_name TEXT, location TEXT, suites TEXT);');
for (const event of log.toString().matchAll(/^\[FAIL\] (.+)$/gm)) database.prepare('INSERT INTO failed_case_events VALUES (?)').run(event[1]);
for (const row of failureDefinitions) database.prepare('INSERT INTO failure_case_definitions VALUES (?, ?, ?, ?, ?)').run(row.id, row.label, row.caseName, row.location, row.suites.join(', '));
const failureSql = read(prefix + 'failure_counts.sql').toString();
const failureRows = database.prepare(failureSql).all();
database.close();
if (failureRows.some(row => row.appearances !== failureDefinitions.find(x => x.id === row.id).suites.length)) throw new Error('Failure chart counts do not match reviewed suite mapping.');
const sources = [
  { id: 'review', label: '현행 코드 위치·판정·반례를 정리한 기술 감사 원문', path: reportPath },
  { id: 'execution', label: '이번 전체 회귀·현재 identity·독립 진단의 보존 결과', path: prefix + 'results.json' },
  { id: 'tests', label: '전체 CTest 원본 로그: 110/116, 중복 실패 포함', path: prefix + 'ctest-full.log' },
  { id: 'failure-counts', label: '원본 CTest FAIL 행의 SQL 중복 집계', path: prefix + 'failure_counts.sql',
    query: { sql: failureSql, engine: 'SQLite', language: 'sql', tables_used: ['main.failed_case_events', 'main.failure_case_definitions'],
      description: 'assemble_report.mjs가 보존된 ctest-full.log의 모든 FAIL 행을 main.failed_case_events에 넣고 네 case 정의와 결합한다. 전체 1회 실행, source closure 제외, 중복은 유지한다.',
      executed_at: capturedAt } },
  { id: 'plan', label: '승인된 전체 48개 구현 단위와 제품 기준', path: 'docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md' },
  { id: 'contract', label: '현재 canonical Full-Scope Beta contract', path: 'docs/product/full-product-beta-contract.json' },
  { id: 'ledger', label: '현행 ledger의 local unit 수락 기록', path: 'docs/implementation/FULL_SCOPE_BETA_EXECUTION.md' },
  { id: 'recovery', label: '정상 복구 뒤 검토 실패의 대조군 포함 진단', path: prefix + 'recovery_review_probe.cpp' },
  { id: 'language', label: '실제 영어 phoneme 역할 및 canonical path 진단', path: prefix + 'language_path_probe.cpp' },
  { id: 'gate', label: '좁은 operation evidence 검사 및 tempo 식의 반례', path: prefix + 'gate_subpredicate_probe.py' },
  { id: 'matrix', label: '현재 actual CLAP engineering matrix, 출시 수락 아님', path: prefix + 'matrix.json' },
  { id: 'producer-code', label: '정확한 take/parent와 commit receipt 구현', path: 'libs/seam-voicebank-production/src/repository_operations.cpp' },
  { id: 'review-code', label: '지원되는 revision-bound review 구현', path: 'libs/seam-voicebank-production/src/repository_review.cpp' },
  { id: 'pipeline-code', label: '현재 Sample/Procedural 렌더 분기와 Neural 미지원 경계', path: 'libs/seam-rendering/src/render_pipeline.cpp' },
  { id: 'host-code', label: 'CLAP offline 준비·process 결과 구현', path: 'libs/seam-clap-editor/src/plugin_entry.cpp' },
];
const blocks = sections.map((body, index) => ({ id: index === 0 ? 'title' : `section-${index}`, type: 'markdown', body: body.trim(),
  ...(index === 0 ? {} : { sourceId: index === 3 ? 'execution' : index === 2 ? 'plan' : 'review' }) }));
blocks.splice(7, 0, { id: 'failure-repeat-chart', type: 'chart', chartId: 'failure-repeats' });
const artifact = { surface: 'report', manifest: { version: 1, surface: 'report', title,
  description: '현재 소스·전체 회귀·독립 반례에 근거한 한국어 심층 리뷰. 제품 코드는 수정하지 않음.',
  generatedAt: capturedAt, sources,
  blocks, charts: [{ id: 'failure-repeats', type: 'bar', title: '개별 실패의 중복 실행 횟수',
    subtitle: '이번 전체 실행의 네 C++ 실패 case. source closure 제외; 제품 결함 수나 완료율이 아니다.', showDescription: true,
    dataset: 'failureRepeats', sourceId: 'failure-counts', valueFormat: 'number',
    encodings: { x: { field: 'label', type: 'nominal', label: '개별 case' }, y: { field: 'appearances', type: 'quantitative', label: '로그의 실패 출력 횟수' } },
    options: { orientation: 'horizontal', legend: false } }] },
  snapshot: { version: 1, status: 'ready', generatedAt: capturedAt, datasets: { failureRepeats: failureRows } }, sources };
fs.writeFileSync(path.join(output, 'artifact.json'), JSON.stringify(artifact, null, 2) + '\n');
console.log(JSON.stringify({ reportPath, sections: sections.length - 1, ctest: `${results.ctest.passed}/${results.ctest.total}`, hashesVerified: true }));
