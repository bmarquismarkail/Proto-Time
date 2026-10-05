// Pure serialization/hash and JavaScript syntax checks, without a browser or DOM.
import { readFileSync } from 'node:fs';
import { Script, createContext } from 'node:vm';
import { webcrypto } from 'node:crypto';
import assert from 'node:assert/strict';

const html = readFileSync(process.argv[2], 'utf8');
const project = JSON.parse(html.match(/<script id="capture" type="application\/json">([\s\S]*?)<\/script>/)[1]);
const scripts = [...html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
assert.equal(scripts.length, 1);
const source = scripts[0][1];
new Script(source); // Parse the full viewer without executing its UI.
const helpers = source.slice(source.indexOf('function canonical('), source.indexOf('async function load('));
const context = createContext({ crypto: webcrypto, TextEncoder, Uint8Array });
new Script(helpers).runInContext(context);
const facts = {};
for (const key of ['romSha256', 'core', 'instructions', 'edges', 'dependencies', 'gaps', 'transfers', 'boundaries'])
    if (key in project) facts[key] = project[key];
context.facts = facts;
assert.equal(await new Script('sha(canonical(facts))').runInContext(context), project.analysis.evidenceDigest);
context.identity = project.romSha256 + ':' + project.analysis.analyzerVersion + ':' + project.analysis.sourceRevision + ':' + project.analysis.evidenceDigest;
assert.equal(await new Script('sha(identity)').runInContext(context), project.analysis.id);
context.project = structuredClone(project);
context.b = project.blocks.find(block => block.instructions.length > 1);
assert.ok(context.b);
const interior = context.b.instructions.at(-1);
context.project.annotations[interior] = [{ kind: 'correction', text: 'Interior instruction note' }];
const annotationExpression = source.match(/JSON.stringify\(b\.instructions\.filter[\s\S]*?,null,2\)/)[0];
const notes = JSON.parse(new Script(annotationExpression).runInContext(context));
assert.ok(notes.some(note => note.instruction === interior && note.annotations[0].text === 'Interior instruction note'));
console.log('Viewer syntax, canonical identity and annotation serialization checks passed; UI not inspected.');
