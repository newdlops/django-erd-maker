// Fast F5 check for the already built, audited checkpoint preview.
// No compilation, model training, geometry search or native layout run.
const fs = require('node:fs'), path = require('node:path');
const assert = require('node:assert/strict'), crypto = require('node:crypto');
const root = path.resolve(__dirname, '../..');
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const resolve = file => path.resolve(root, file);
const provenanceFile = resolve('data/erd-poc/candidates/captain-ml-latest-checkpoints.provenance.json');
const provenance = read(provenanceFile);
assert.equal(provenance.latestNetworkForwardExported, true);
assert.equal(provenance.newTrainingUpdates, 0);
assert.equal(provenance.coordinateSearchOrRepairs, 0);
assert.equal(sha(resolve(provenance.candidate)), provenance.candidateSha256);
assert.equal(sha(resolve(provenance.bestCandidate)), provenance.bestCandidateSha256);
const audit = read(resolve(provenance.audit));
assert.equal(sha(resolve(provenance.audit)), provenance.auditSha256);
assert.equal(audit.candidateSha256, provenance.candidateSha256);
assert.equal(audit.actualProductFileLoadVerified, true);
assert.equal(audit.roundTripViewSwitchVerified, true);
assert.equal(audit.completeIndividualGeometryPreserved, true);
assert.equal(audit.models, 1244);
assert.equal(audit.canonicalRelationships, 1727);
for (const view of Object.values(provenance.checkpoints)) {
  assert.equal(sha(resolve(view.checkpoint)), view.checkpointSha256);
  assert.equal(sha(resolve(view.observations)), view.observationsSha256);
}
for (const file of provenance.buildBindings) {
  assert.equal(sha(resolve(file.path)), file.sha256, `App files changed; rebuild and reverify preview: ${file.path}`);
}
for (const file of provenance.inferenceBindings) {
  assert.equal(sha(resolve(file.path)), file.sha256, file.path);
}
console.log(`Latest checkpoint preview ready: overview ${audit.overviewVisual} / individual ${audit.individualVisual}.`);
console.log('Django ERD: Open Diagram; turn off June bundles and Leaf cards for the individual view.');
