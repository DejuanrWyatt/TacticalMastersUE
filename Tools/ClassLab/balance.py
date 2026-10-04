"""The balance pass: screen every class, shrink the noise out, confirm on fresh battles.

The class lab (TMClassLab.exe, Tools/ClassLab/ClassLab.cpp) answers one question about one class: how often does a
team with it win? This runs it the way the 2026-10-03 research report ("Balancing tactical games") says it should be
run, so that a verdict is evidence and not luck (Docs/design/feat-class-balance.md, "The lab"):

1. Screen. Every class file, 80 battles each, across the four standard reference teams and every map, on the
   lab's usual seeds (seed base 0).
2. Shrink. At 80 battles a class that truly wins 50% shows anywhere from about 39% to 61%. Each class's rate is
   pulled toward its role's average by as much as the noise says it should be (empirical Bayes), so the luckiest
   and unluckiest classes stop looking like the strongest and weakest.
3. Confirm. Only the classes whose shrunk estimate is probably outside the 40-60% band are played again, on seeds
   the screen never used (seed base 1, 2, 3), 80 battles at a time, stopping as soon as the 95% interval of the new
   battles settles it: clearly outside the band, clearly inside it, or (after three rounds) still unclear.
4. With --search, every confirmed weak class is also played by the search player. A class that gains much more
   from better play than the in-band classes do is being under-played by the computer, not under-powered.
5. With --ratings, each role's round-robin (lab "rating"), with --sample classes each.

It writes balance.json (everything), balance.csv (one row a class) and balance.md (the verdicts) into --out. It
changes no class file: what to do about a verdict is a person's call.

    python Tools\\ClassLab\\balance.py [--lab Binaries\\ClassLab\\TMClassLab.exe] [--data Content\\Data]
        [--out Saved\\Balance] [--games 80] [--search] [--ratings] [--sample 12] [--jobs N] [--only id,id]

Standard library only. Run from the project folder (Tools\\ClassLab\\Balance.bat does).
"""

import argparse
import csv
import json
import math
import os
import statistics
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor

BAND = (40.0, 60.0)
Z95 = 1.959964


def wilson(wins, games):
    """The 95% Wilson interval of a win rate, in percent."""
    if games <= 0:
        return (0.0, 100.0)
    p = wins / games
    denominator = 1 + Z95 * Z95 / games
    centre = (p + Z95 * Z95 / (2 * games)) / denominator
    half = Z95 * math.sqrt(p * (1 - p) / games + Z95 * Z95 / (4 * games * games)) / denominator
    return (100 * max(0.0, centre - half), 100 * min(1.0, centre + half))


def normal_cdf(x):
    return 0.5 * (1 + math.erf(x / math.sqrt(2)))


class Lab:
    def __init__(self, exe, data, teams):
        self.exe = exe
        self.data = data
        self.teams = teams
        self.classes = os.path.join(data, 'Classes')

    def base_args(self):
        args = ['--maps', os.path.join(self.data, 'Maps'), '--monsters', os.path.join(self.data, 'Monsters')]
        if self.teams:
            args += ['--teams', self.teams, '--classes', self.classes]
        return args

    def run(self, args, tag):
        done = subprocess.run([self.exe] + args + self.base_args(), capture_output=True, text=True)
        lines = [line for line in done.stdout.splitlines() if line.startswith(tag)]
        if not lines:
            raise RuntimeError('the lab said nothing for %s: %s' % (' '.join(args), (done.stderr or done.stdout)[-400:]))
        return lines

    def playtest(self, path, games, seed_base=0, skill='hard', ban=None):
        args = ['playtest', path, str(games), '--seed-base', str(seed_base), '--skill', skill]
        if ban is not None:
            args += ['--ban', str(ban)]
        result = json.loads(self.run(args, 'PLAYTEST ')[0][len('PLAYTEST '):])
        if not result.get('ok'):
            raise RuntimeError('%s: %s' % (path, result.get('errors')))
        return result

    def rating(self, role, games, sample):
        args = ['rating', self.classes, role, str(games)]
        if sample:
            args += ['--sample', str(sample)]
        lines = self.run(args, 'RATING')
        return [json.loads(line.split(' ', 1)[1]) for line in lines if line.startswith('RATING ')]


def class_files(data, only):
    folder = os.path.join(data, 'Classes')
    found = {}
    for name in sorted(os.listdir(folder)):
        if name.endswith('.tmclass.json'):
            with open(os.path.join(folder, name), encoding='utf-8') as handle:
                document = json.load(handle)
            if only and document['id'] not in only:
                continue
            found[document['id']] = {'path': os.path.join(folder, name), 'name': document.get('name', document['id']),
                                     'role': (document.get('roles') or ['damage'])[0]}
    return found


def shrink(rows):
    """Empirical Bayes by role: each rate pulled toward its role's mean by its share of the noise."""
    by_role = {}
    for row in rows.values():
        by_role.setdefault(row['role'], []).append(row)
    for role, members in by_role.items():
        rates = [m['wins'] / m['games'] for m in members]
        mean = statistics.fmean(rates)
        noise = statistics.fmean([r * (1 - r) / m['games'] for r, m in zip(rates, members)]) if members else 0.0
        spread = statistics.pvariance(rates) if len(rates) > 1 else 0.0
        between = max(spread - noise, 1e-6)
        for rate, member in zip(rates, members):
            variance = max(rate * (1 - rate), 0.05) / member['games']
            weight = between / (between + variance)
            estimate = mean + weight * (rate - mean)
            sd = math.sqrt(weight * variance)
            member['roleMean'] = 100 * mean
            member['shrunk'] = 100 * estimate
            member['shrunkSd'] = 100 * sd
            member['weight'] = weight
            low = normal_cdf((BAND[0] / 100 - estimate) / sd)
            high = 1 - normal_cdf((BAND[1] / 100 - estimate) / sd)
            member['pOutside'] = low + high
            member['side'] = 'weak' if low > high else 'strong'


def confirm(lab, row, games, rounds):
    """Fresh battles, 80 at a time, until the interval settles it."""
    wins = 0.0
    played = 0
    verdict = 'unclear'
    for seed_base in range(1, rounds + 1):
        result = lab.playtest(row['path'], games, seed_base=seed_base)
        wins += result['wins']
        played += result['games']
        low, high = wilson(wins, played)
        if high < BAND[0]:
            verdict = 'weak'
            break
        if low > BAND[1]:
            verdict = 'strong'
            break
        if low >= BAND[0] - 5 and high <= BAND[1] + 5:
            verdict = 'in band'
            break
    low, high = wilson(wins, played)
    row['confirm'] = {'games': played, 'win': round(100 * wins / played, 1), 'ci': [round(low), round(high)],
                      'verdict': verdict}


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    root = os.path.normpath(os.path.join(here, '..', '..'))
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    exe = 'TMClassLab.exe' if os.name == 'nt' else 'TMClassLab'
    parser.add_argument('--lab', default=os.path.join(root, 'Binaries', 'ClassLab', exe))
    parser.add_argument('--data', default=os.path.join(root, 'Content', 'Data'))
    parser.add_argument('--out', default=os.path.join(root, 'Saved', 'Balance'))
    parser.add_argument('--teams', default='standard', help='"standard", "classic" for the old single team, or a list')
    parser.add_argument('--games', type=int, default=80)
    parser.add_argument('--rounds', type=int, default=3, help='confirmation rounds at most')
    parser.add_argument('--threshold', type=float, default=0.8, help='chance outside the band that flags a class')
    parser.add_argument('--search', action='store_true')
    parser.add_argument('--ratings', action='store_true')
    parser.add_argument('--sample', type=int, default=12)
    parser.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) - 1))
    parser.add_argument('--only', default='', help='class ids, comma separated')
    parser.add_argument('--resume', action='store_true', help='reuse the stages a stopped run saved in --out')
    options = parser.parse_args()

    if not os.path.exists(options.lab):
        sys.exit('No class lab at %s: run Tools\\ClassLab\\Build.bat first.' % options.lab)
    lab = Lab(options.lab, options.data, '' if options.teams == 'classic' else options.teams)
    only = {i for i in options.only.split(',') if i}
    rows = class_files(options.data, only)
    os.makedirs(options.out, exist_ok=True)
    started = time.time()
    pool = ThreadPoolExecutor(max_workers=options.jobs)

    def say(text):
        print('[%4.0fs] %s' % (time.time() - started, text), flush=True)

    # Each stage is saved as it finishes (progress.json), so a long run that stops can carry on with --resume.
    progress_path = os.path.join(options.out, 'progress.json')
    stages = []
    if options.resume and os.path.exists(progress_path):
        with open(progress_path, encoding='utf-8') as handle:
            saved = json.load(handle)
        if set(saved['classes']) == set(rows) and saved.get('games') == options.games and saved.get('teams') == options.teams:
            for class_id, row in saved['classes'].items():
                rows[class_id].update(row)
            stages = saved['stages']
            say('resume: %s already done' % ', '.join(stages))

    def save(stage):
        stages.append(stage)
        with open(progress_path, 'w', encoding='utf-8') as handle:
            json.dump({'games': options.games, 'teams': options.teams, 'stages': stages, 'classes': rows}, handle)

    if 'screen' not in stages:
        say('screen: %d classes, %d battles each, %s teams, %d at a time' % (len(rows), options.games, options.teams, options.jobs))
        for class_id, result in zip(rows, pool.map(lambda r: lab.playtest(r['path'], options.games), rows.values())):
            row = rows[class_id]
            row.update({'games': result['games'], 'wins': result['wins'], 'win': result['win'], 'ci': result['ci'],
                        'margin': result['margin'], 'stack': result['stack'], 'perGame': result['perGame'],
                        'unused': result['unused'], 'illegal': result['illegal']})
        save('screen')
    shrink(rows)
    flagged = [r for r in rows.values() if r['pOutside'] >= options.threshold]
    say('shrink: %d of %d flagged (chance outside %d-%d%% at least %.0f%%)'
        % (len(flagged), len(rows), BAND[0], BAND[1], 100 * options.threshold))

    if 'confirm' not in stages:
        list(pool.map(lambda r: confirm(lab, r, options.games, options.rounds), flagged))
        save('confirm')
        say('confirm: done on fresh seeds')

    if options.search and 'search' not in stages:
        weak = [r for r in flagged if r['confirm']['verdict'] == 'weak']
        controls = sorted((r for r in rows.values() if abs(r['shrunk'] - 50) < 5), key=lambda r: r['path'])[:6]
        for row, result in zip(weak + controls,
                               pool.map(lambda r: lab.playtest(r['path'], options.games, skill='search'), weak + controls)):
            row['search'] = {'win': result['win'], 'uplift': result['win'] - row['win'],
                             'changed': result.get('searchChanged', 0), 'turns': result.get('searched', 0)}
        typical = statistics.median([c['search']['uplift'] for c in controls]) if controls else 0
        for row in weak:
            row['search']['typical'] = typical
            row['search']['underplayed'] = row['search']['uplift'] >= typical + 10
        save('search')
        say('search: %d weak classes and %d controls (typical uplift %+.0f)' % (len(weak), len(controls), typical))

    ratings = {}
    if options.ratings:
        roles = sorted({r['role'] for r in rows.values()})
        for role, role_ratings in zip(roles, pool.map(lambda role: lab.rating(role, 4, options.sample), roles)):
            ratings[role] = role_ratings
            say('ratings: %s, %d classes' % (role, len(role_ratings)))
        for role_ratings in ratings.values():
            for entry in role_ratings:
                if entry['id'] in rows:
                    rows[entry['id']]['rating'] = entry['rating']
                    rows[entry['id']]['ratingSe'] = entry['se']

    def verdict(row):
        if 'confirm' in row:
            return row['confirm']['verdict']
        return 'in band' if BAND[0] <= row['shrunk'] <= BAND[1] else 'probably in band'

    report = {'when': time.strftime('%Y-%m-%d %H:%M'), 'games': options.games, 'teams': options.teams,
              'band': BAND, 'threshold': options.threshold, 'classes': rows, 'ratings': ratings}
    with open(os.path.join(options.out, 'balance.json'), 'w', encoding='utf-8') as handle:
        json.dump(report, handle, indent=1)
    columns = ['id', 'name', 'role', 'win', 'ci', 'shrunk', 'pOutside', 'confirmWin', 'confirmCi', 'verdict',
               'searchUplift', 'underplayed', 'rating', 'ratingSe', 'unused', 'margin', 'stack']
    with open(os.path.join(options.out, 'balance.csv'), 'w', newline='', encoding='utf-8') as handle:
        writer = csv.writer(handle)
        writer.writerow(columns)
        for class_id, row in sorted(rows.items(), key=lambda item: (item[1]['role'], item[1]['shrunk'])):
            writer.writerow([class_id, row['name'], row['role'], row['win'], '%d-%d' % tuple(row['ci']),
                             round(row['shrunk'], 1), round(row['pOutside'], 2),
                             row.get('confirm', {}).get('win', ''),
                             '%d-%d' % tuple(row['confirm']['ci']) if 'confirm' in row else '', verdict(row),
                             row.get('search', {}).get('uplift', ''), row.get('search', {}).get('underplayed', ''),
                             row.get('rating', ''), row.get('ratingSe', ''), ' '.join(map(str, row['unused'])),
                             row['margin'], row['stack']])
    lines = ['# Balance pass, %s' % report['when'], '',
             '%d classes, %d battles each on the %s teams; flagged when the shrunk estimate was at least %.0f%% likely '
             'outside %d-%d%%, then confirmed on fresh seeds.' % (len(rows), options.games, options.teams,
                                                                 100 * options.threshold, BAND[0], BAND[1]), '']
    for title, wanted in (('Confirmed weak', 'weak'), ('Confirmed strong', 'strong'), ('Still unclear', 'unclear')):
        chosen = [r for r in rows.values() if verdict(r) == wanted]
        lines += ['## %s (%d)' % (title, len(chosen)), '']
        if chosen:
            lines += ['| Class | Role | Screen | Fresh seeds | 95% interval | Search uplift | Never used |',
                      '|---|---|---|---|---|---|---|']
            for row in sorted(chosen, key=lambda r: r['confirm']['win']):
                search = row.get('search')
                uplift = '' if not search else '%+d%s' % (search['uplift'], ' (under-played)' if search['underplayed'] else '')
                lines.append('| %s | %s | %d%% | %.0f%% | %d-%d%% | %s | %s |' % (
                    row['name'], row['role'], row['win'], row['confirm']['win'], row['confirm']['ci'][0],
                    row['confirm']['ci'][1], uplift, ', '.join('slot %d' % (s + 1) for s in row['unused'])))
        lines.append('')
    never = [r for r in rows.values() if r['unused']]
    lines += ['## Abilities the computer almost never uses (%d classes)' % len(never), '',
              ', '.join('%s (slot %s)' % (r['name'], '/'.join(str(s + 1) for s in r['unused'])) for r in sorted(never, key=lambda r: r['name'])), '']
    with open(os.path.join(options.out, 'balance.md'), 'w', encoding='utf-8') as handle:
        handle.write('\n'.join(lines))
    say('written to %s' % options.out)


if __name__ == '__main__':
    main()
