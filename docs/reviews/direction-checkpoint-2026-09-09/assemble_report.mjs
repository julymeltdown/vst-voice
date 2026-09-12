// Package the reviewed snapshot; never change product sources or tests.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { DatabaseSync } from 'node:sqlite';

const output = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(output, '../../..');
const prefix = 'docs/reviews/direction-checkpoint-2026-09-09/';
const reportPath = 'DEVELOPMENT_DIRECTION_CHECKPOINT_REVIEW_2026-09-09_EN.md';
const sha = value => crypto.createHash('sha256').update(value).digest('hex');
const read = name => fs.readFileSync(path.join(root, name));
const run = (command, args) => execFileSync(command, args, {cwd: root, maxBuffer: 32*1024*1024});
const expectedDiff = '3f320eee76395f944732afcb5a924b978aa6096fe0766d9f9f8d25c8c0297bcd';
if (sha(run('git',['diff','--binary'])) !== expectedDiff)
  throw new Error('Tracked source changed after this review. Do not relabel its evidence.');
const log = read('build/release/Testing/Temporary/LastTest.log');
const testEntries = [...log.toString().matchAll(/^\d+\/120 Testing: (.+)$/gm)].map(match => match[1]);
const failed = read('build/release/Testing/Temporary/LastTestsFailed.log').toString().trim().split('\n').map(line => line.slice(line.indexOf(':')+1));
if (testEntries.length !== 25 || failed.join(',') !== 'seam_phase12b_tests,seam_tracked_source_closure')
  throw new Error('Current CTest logs no longer describe the reviewed 25-target run.');
const summary = read(prefix+'ctest-focused.log').toString();
if (!summary.includes('Total Test time (real) =  28.18 sec') || !summary.includes('SOURCE_CLOSURE=FAIL required_inputs=335'))
  throw new Error('Focused execution differs from the reviewed result.');
fs.copyFileSync(path.join(root,'build/release/Testing/Temporary/LastTest.log'),path.join(output,'ctest-cases.log'));
fs.copyFileSync(path.join(root,'build/release/Testing/Temporary/LastTestsFailed.log'),path.join(output,'ctest-failures.txt'));
for (const file of ['matrix.json','soak-smoke.json'])
  fs.copyFileSync(path.join(root,'build/release/phase12c',file),path.join(output,file));

const matrix = JSON.parse(read(prefix+'matrix.json'));
const soak = JSON.parse(read(prefix+'soak-smoke.json'));
if (matrix.cases !== 336 || matrix.failures !== 0 || matrix.executionPath !== 'clap-plugin-process-v1' || matrix.releaseEligible !== false)
  throw new Error('Matrix identity or result differs.');
if (soak.executionPath !== 'linked-engine-v1' || soak.elapsedSeconds !== 5 || soak.releaseEligible !== false)
  throw new Error('Do not promote linked-engine smoke to actual host qualification.');
const contract = JSON.parse(read('docs/product/full-product-beta-contract.json'));
const currentUntracked = run('git',['ls-files','--others','--exclude-standard','-z']).toString().split('\0').filter(Boolean);
const untrackedProduct = currentUntracked.filter(name => /^(apps|libs|tests|tools|scripts|phase12c|cmake)\//.test(name)).sort();
const sourceHashes = Object.fromEntries(untrackedProduct.map(name => [name,sha(read(name))]));
const capturedAt = new Date().toISOString();
const registeredTests = JSON.parse(run('ctest',['--test-dir','build/release','--show-only=json-v1']).toString()).tests;
if(registeredTests.length!==120) throw new Error('Registered test inventory changed.');
const database = new DatabaseSync(':memory:');
database.exec('CREATE TABLE registered_test_results (name TEXT PRIMARY KEY, outcome TEXT NOT NULL);');
for(const test of registeredTests) {
  const outcome = !testEntries.includes(test.name) ? 'Not executed' : test.name === 'seam_phase12b_tests' ? 'Runtime failure'
    : test.name === 'seam_tracked_source_closure' ? 'Source-closure failure' : 'Passed';
  database.prepare('INSERT INTO registered_test_results VALUES (?,?)').run(test.name,outcome);
}
const coverageSql = read(prefix+'test_coverage.sql').toString();
const coverageRows = database.prepare(coverageSql).all();
database.close();
const results = {
  reviewDate:'2026-09-09',timezone:'Asia/Seoul',capturedAt,
  head:run('git',['rev-parse','HEAD']).toString().trim(),
  branch:run('git',['branch','--show-current']).toString().trim(),
  trackedDiffSha256:expectedDiff,
  scope:'Read-only review of product source and tests. Only report artifacts/logs are written. No staging, commit, push, release approval or product mutation.',
  verdict:{architecture:'RETAIN',execution:'CAPABILITY_FIRST_REFOCUS',integration:'NOT_ACCEPTED',fullScopeBeta:'NO_GO'},
  beforeReportArtifacts:{trackedChanged:215,untrackedFiles:336,trackedInsertions:23809,trackedDeletions:1250,untrackedLinesExcluded:true},
  build:{command:'cmake --build build/release -j 4',exitCode:1,log:prefix+'build-full.log',
    error:'test_studio_manifest_draft.cpp:293,309,340 calls private selectedAudioPath()',
    affectedTargets:['seam_tests','seam_studio_manifest_draft_tests'],
    focusedTargetsRebuilt:true,fullCurrentCoreRun:false},
  ctest:{registered:120,executed:25,passed:23,failed:2,seconds:28.18,failedSuites:failed,
    resultIsFullSuite:false,resultIsProductCompletion:false,unindexedRequiredInputs:335,
    command:'ctest --test-dir build/release --output-on-failure -j 1 -R <exact 25-target selection> --output-log '+prefix+'ctest-focused.log',
    selectedTargets:testEntries,
    cases:{producer:38,manifestDraft:11,wavLimits:5,studioReview:9,sampleReviewCli:3,language:20,performanceCompiler:19,offlineSession:3},
    latestStudioDraft:{writtenCases:16,execution:'NOT_RUN_CURRENT_SOURCE_COMPILE_FAILURE'}},
  python:{command:'python3 -m unittest tests.production.test_production_draft_parity tests.production.test_voice_source_admission tests.external_beta.test_full_product_gate tests.production.test_public_release_state_machine',tests:33,seconds:9.367,exitCode:0},
  matrix:{cases:matrix.cases,expected:matrix.expected,failures:matrix.failures,executionPath:matrix.executionPath,
    evidenceScope:matrix.evidenceScope,resourceMode:matrix.resourceMode,releaseEligible:matrix.releaseEligible,pluginSha256:matrix.pluginSha256},
  soak:{executionPath:soak.executionPath,seconds:soak.elapsedSeconds,releaseEligible:soak.releaseEligible,notActualPluginSoak:true},
  contract:{requirements:contract.requirements.length,cases:contract.cases.length,hostTuples:contract.scope.hostTuples.length,
    matrixStatus:contract.scope.matrixStatus,releasedResources:contract.scope.releasedResources.length,
    evaluationProfileStatus:contract.evaluationProfile.status,
    criteria:contract.evaluationProfile.criteria.reduce((counts,row)=>(counts[row.status]=(counts[row.status]||0)+1,counts),{})},
  ledger:{historicallyAccepted:['U1','U2','U3','U4','U5'],totalUnits:48,ratio:5/48,isFreshReacceptance:false,isProductCompletion:false},
  phase12b:{directRerunExitCode:40,stdout:'',
    debuggerObservation:'Second TechnicalEditController::selectUnitVariant returned Unit plan entry is unavailable for this phoneme; nucleus lookup and boundary edit succeeded.',
    diagnosis:'Fixture consumes retained partial technical render plan before replacement after undo.',
    evidenceClass:'Direct root execution plus independent targeted debugger inspection; no product/test edits.'},
  gateProbe:{reference:'README.md',subpredicateErrors:[],isFullReportOrGoBypass:false,
    probe:'docs/reviews/direction-post-integration-2026-09-09/gate_subpredicate_probe.py',
    referenceSha256:sha(read('README.md'))},
  sourceHashes,
  sourceHashesSha256:sha(JSON.stringify(sourceHashes)),
  artifactNotes:{audience:'technical',delivery:'html',language:'English as requested in the latest language correction',
    reportStatusMeans:'The review is ready; the product is not release-ready.',
    structure:'Technical summary 1; definitions 2; current evidence 3; findings 4-13; next steps 14; limitations and questions 15. Methods integrated in 2,3,15.',
    visualizationOmission:'No meaningful product completion or acoustic time series; no product completion score is estimated.',
    chartContract:{question:'How much of the registered CTest inventory was actually verified now?',takeaway:'Focused passing results do not establish the unexecuted majority.',
      family:'Comparison',variant:'horizontal bar',rows:4,grain:'Mutually exclusive current CTest execution statuses',registeredDenominator:120,
      coverage:'23 pass, one runtime failure, one closure failure, 95 not executed',palette:'Single-root shared reader palette; direct category labels, no color grouping or legend',
      footprint:'Full-width report block immediately after section 3 with adjacent explanatory prose',qa:'Canonical portable reader and semantic fallback; desktop and narrow viewports'},
    localLinkTransformation:'Portable reader replaces machine-local file hyperlinks with exact repository-relative file:line text; source Markdown keeps clickable local links.'},
  notRun:['Fresh clean-checkout full build','Fresh current core and Studio draft execution','Windows runtime','Complete latest native visual/input QA',
    'Nine signed-installed DAW tuples','Long host qualification soak','Qualified female singer listening','Actual neural singer model qualification','Independent creator/language/music acceptance']
};
fs.writeFileSync(path.join(output,'results.json'),JSON.stringify(results,null,2)+'\n');
const report = read(reportPath).toString();
const title = report.split('\n')[0].replace(/^# /,'');
const machinePrefix = root+'/';
const portable = report.replace(/\[([^\]]+)\]\((\/[^)]+)\)/g,(match,label,target)=>{
  if (!target.startsWith(machinePrefix)) throw new Error('Unexpected local report link '+target);
  const relative = target.slice(machinePrefix.length);
  const file = relative.replace(/:\d+$/,'');
  if (!fs.existsSync(path.join(root,file))) throw new Error('Missing cited source '+relative);
  return label+' (`'+relative+'`)';
});
const sources = [
  {id:'review',label:'Current source-level review and decision rationale',path:reportPath},
  {id:'execution',label:'Current build, focused execution, matrix and contract snapshot',path:prefix+'results.json'},
  {id:'build',label:'Fresh full Release build compiler failure',path:prefix+'build-full.log'},
  {id:'ctest',label:'Individual execution log for the 25 selected CTest entries',path:prefix+'ctest-cases.log'},
  {id:'matrix',label:'Actual CLAP process matrix: 336 engineering cases',path:prefix+'matrix.json'},
  {id:'soak',label:'Five-second linked-engine smoke, not actual plugin qualification',path:prefix+'soak-smoke.json'},
  {id:'plan',label:'Approved full scope: 48 units and R1-R20',path:'docs/plans/2026-09-05-1718-feat-full-scope-beta-go-plan.md'},
  {id:'contract',label:'Current unresolved resource matrix and evaluation criteria',path:'docs/product/full-product-beta-contract.json'},
  {id:'ledger',label:'Historical acceptance and incomplete implementation records',path:'docs/implementation/FULL_SCOPE_BETA_EXECUTION.md'},
  {id:'coverage-query',label:'CTest registry reconciled with the current focused execution log',path:prefix+'test_coverage.sql',
    query:{sql:coverageSql,engine:'SQLite',language:'sql',tables_used:['main.registered_test_results'],executed_at:capturedAt,
      description:'assemble_report.mjs loads the current 120-entry CTest registry and joins it by exact test name to the retained 25-entry LastTest log. Nonselected entries are not executed, not failures.'}}
];
const blocks = portable.split(/(?=^## )/m).map((body,index)=>({
  id:index===0?'title':'section-'+index,type:'markdown',body:body.trim(),
  ...(index===0?{}:{sourceId:index===3?'execution':'review'})
}));
if(coverageRows.reduce((sum,row)=>sum+row.entries,0)!==results.ctest.registered) throw new Error('CTest inventory does not reconcile.');
blocks.splice(4,0,{id:'current-test-coverage',type:'chart',chartId:'test-coverage'});
const artifact = {surface:'report',manifest:{version:1,surface:'report',title,
  description:'Deep review based on current source, fresh rebuilds and focused regressions. Product source unchanged.',
  generatedAt:capturedAt,sources,blocks,charts:[{id:'test-coverage',type:'bar',title:'Current CTest verification coverage',
    subtitle:'120 registered entries; only 25 executed in this review. Not product completion.',showDescription:true,
    dataset:'testCoverage',sourceId:'coverage-query',valueFormat:'number',
    encodings:{x:{field:'status',type:'nominal',label:'Execution status'},y:{field:'entries',type:'quantitative',label:'CTest entries'}},
    options:{orientation:'horizontal',legend:false}}]},snapshot:{version:1,status:'ready',generatedAt:capturedAt,datasets:{testCoverage:coverageRows}},sources};
fs.writeFileSync(path.join(output,'artifact.json'),JSON.stringify(artifact,null,2)+'\n');
console.log(JSON.stringify({sections:blocks.length-1,ctest:'23/25 focused only',fullBuild:'FAIL',sourceUnchanged:true,reportPath}));
