#!/usr/bin/env python3
"""Paired v2 criteria. No function changes execution policy or family admission."""
import hashlib
import json
import math
from pathlib import Path
import random
import statistics

ROOT = Path(__file__).resolve().parents[1]
CRITERIA_PATH = ROOT / 'tools/acceleration-criteria-v2.json'


def load_criteria():
    return json.loads(CRITERIA_PATH.read_text())


def criteria_binding(root=ROOT):
    path = root / 'tools/acceleration-criteria-v2.json'
    criteria = json.loads(path.read_text())
    authority = root / criteria['authority']
    return dict(version=criteria['version'], criteriaSha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                authoritySha256=hashlib.sha256(authority.read_bytes()).hexdigest())


def paired_statistics(log_ratios, criteria=None):
    c = criteria or load_criteria()
    n = len(log_ratios)
    if n not in (c['repetitions'], c['inconclusiveFollowupRepetitions']):
        raise ValueError('requires nine pairs or registered 27-pair follow-up')
    if any(not math.isfinite(x) for x in log_ratios):
        raise ValueError('nonfinite paired ratios')
    rng = random.Random(c['bootstrapSeed'])
    draws = sorted(statistics.median(log_ratios[rng.randrange(n)] for _ in range(n))
                   for _ in range(c['bootstrapResamples']))
    tail = (1 - c['confidence']) / 2
    lo = draws[int(tail * (len(draws) - 1))]
    hi = draws[math.ceil((1 - tail) * (len(draws) - 1))]
    gains = [100 * (1 - math.exp(x)) for x in log_ratios]
    lower, upper = 100 * (1 - math.exp(hi)), 100 * (1 - math.exp(lo))
    median = 100 * (1 - math.exp(statistics.median(log_ratios)))
    loss = c['maximumRegressionPercent']
    epsilon = 1e-9  # Only floating-point arithmetic at an exact threshold.
    repeatable = median < -loss-epsilon or sum(x < -loss-epsilon for x in gains) > n // 2
    nonregression = 'failed' if repeatable else 'passed' if lower >= -loss-epsilon else 'inconclusive'
    positives = sum(x > 0 for x in gains)
    benefit = lower > 0 and positives >= math.ceil(n * c['minimumPositivePairFraction'])
    return dict(pairedMedianImprovementPercent=median, confidenceIntervalPercent=[lower, upper],
                confidence=c['confidence'], bootstrapResamples=c['bootstrapResamples'],
                bootstrapSeed=c['bootstrapSeed'], method='percentile bootstrap of paired median log ratios',
                positivePairs=positives, repetitions=n, repeatableRegression=repeatable,
                nonRegression=nonregression, activeBenefit=benefit, benefitStatus='passed' if benefit else 'failed' if upper <= 0 else 'inconclusive',
                pairedImprovementPercent=gains, logRatios=list(log_ratios))


def analyze_report(report, candidate, expected_variant=None, criteria=None):
    # Rebuild comparisons from raw samples, never trust supplied summary/decisions.
    from measure_acceleration_experiments import summarize
    c = criteria or load_criteria()
    synthetic = report['schema'] == 'proto-time-acceleration-synthetic-experiments-v2'
    if not synthetic and report['schema'] != 'proto-time-acceleration-experiments-v2':
        raise ValueError('v2 requires fresh v2 raw reports')
    if report.get('profileTimings') or report.get('diagnosticBuild'):
        raise ValueError('diagnostic timings cannot qualify')
    if report['repetitions'] not in (c['repetitions'], c['inconclusiveFollowupRepetitions']):
        raise ValueError('smoke/screen timing cannot qualify')
    rows = [r for r in summarize(report) if r['candidate'] == candidate]
    if not synthetic:
        if not report['retirementWindows']:
            raise ValueError('missing complete retirement windows')
        if any(r['windowCount'] != 35 or r['retirementsPerWindow'] != 128 for r in report['correctness']):
            raise ValueError('incomplete retirement windows')
        if {r['variant'] for r in rows} != {expected_variant}:
            raise ValueError('candidate variant changed')
    expected = {(core, w) for core in c['cores'] for w in
                c['activeRequiredWorkloads' if synthetic else 'representativeRequiredWorkloads']}
    if {(r['core'], r['workload']) for r in rows} != expected or len(rows) != len(expected):
        raise ValueError('missing/duplicate required workloads')
    result = []
    for row in rows:
        samples = [s for s in report['samples'] if s['core'] == row['core'] and s['workload'] == row['workload']]
        pairs = {}
        for s in samples:
            pairs.setdefault(s['repeat'], {})[s['backend']] = s
        logs = [math.log(pairs[i][candidate]['nanoseconds'] / pairs[i][row['comparator']]['nanoseconds'])
                for i in range(report['repetitions'])]
        stats = paired_statistics(logs, c)
        # Preserve the original ratio-of-medians and majority rule as well.
        if row['improvementPercent'] < -c['maximumRegressionPercent']-1e-9 or sum(g < -c['maximumRegressionPercent']-1e-9 for g in row['pairedImprovementPercent']) > report['repetitions']//2:
            stats['repeatableRegression'] = True
            stats['nonRegression'] = 'failed'
        result.append(dict(core=row['core'], workload=row['workload'], comparator=row['comparator'],
                           historicalRatioOfMediansPercent=row['improvementPercent'], **stats))
    return result


def decisions(sessions, latency_passed, diverse_active_roms=False, criteria=None):
    c = criteria or load_criteria()
    if len(sessions) != c['sessions'] or len({s['sessionId'] for s in sessions}) != c['sessions']:
        raise ValueError('two independent confirmation sessions required')
    identity = ('sourceSha256', 'criteriaBinding', 'candidate', 'buildBinding', 'registrationSha256')
    for key in identity:
        if sessions[0][key] != sessions[1][key]:
            raise ValueError('changed session identity: ' + key)
    output = {}
    for core in c['cores']:
        failures, uncertain, active_ok, representative_ok = [], [], True, True
        aggregates = []
        for session in sessions:
            for domain in ('active', 'representative'):
                rows = [r for r in session[domain] if r['core'] == core]
                expected = c['activeRequiredWorkloads' if domain == 'active' else 'representativeRequiredWorkloads']
                if {r['workload'] for r in rows} != set(expected) or len(rows) != len(expected):
                    raise ValueError('incomplete analyzed session')
                for r in rows:
                    if r['nonRegression'] == 'failed': failures.append(f"{session['sessionId']}/{domain}/{r['workload']}: regression")
                    if r['nonRegression'] == 'inconclusive': uncertain.append(f"{session['sessionId']}/{domain}/{r['workload']}: uncertain regression margin")
                if domain == 'active':
                    active_ok &= all(r['activeBenefit'] for r in rows if r['workload'] in c['activeBenefitWorkloads'])
                else:
                    n = rows[0]['repetitions']
                    if any(r['repetitions'] != n for r in rows): raise ValueError('unequal aggregate pairs')
                    aggregate = paired_statistics([statistics.mean(r['logRatios'][i] for r in rows) for i in range(n)], c)
                    aggregates.append(aggregate)
                    representative_ok &= aggregate['confidenceIntervalPercent'][0] > 0
        if not active_ok:
            active_rows=[r for s in sessions for r in s['active'] if r['core']==core and r['workload'] in c['activeBenefitWorkloads']]
            if any(r['benefitStatus']=='failed' for r in active_rows): failures.append('preregistered active benefit absent in at least one session')
            else: uncertain.append('preregistered active benefit not established in both sessions')
        if not latency_passed: failures.append('full suite or required latency evidence failed/not run')
        opt_in = not failures and not uncertain
        output[core] = dict(status='competitive-opt-in-awaiting-review' if opt_in else 'failed' if failures else 'inconclusive',
                            failures=failures, inconclusive=uncertain, competitiveOptInEligible=opt_in,
                            defaultEligible=opt_in and representative_ok and diverse_active_roms,
                            representativeBenefit=bool(representative_ok), diverseActiveRomsVerified=diverse_active_roms,
                            representativeAggregates=aggregates)
    return dict(schema='proto-time-acceleration-decision-v2', cores=output,
                admissionChanged=False, defaultPolicyChanged=False, nativeArm64='user-deferred; not evaluated')
