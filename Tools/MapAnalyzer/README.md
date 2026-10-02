# The Map Analyser

Measures what a battle map will play like, using the rules themselves (`Source/TMSim`), with no engine and
in well under a second. It builds the map exactly as a battle does (`FMap::BuildMirrored`) and asks the
rules for everything:
- walking uses the pathfinder's own distance field
- seeing uses `HasLineOfSight`
- tempo uses `MoveOf` and the Turn Gauge

Its numbers move when the rules do, and never disagree with the game.

```
scripts\map-analyze.bat                              every map in Tools\MapAnalyzer\maps
scripts\map-analyze.bat Tools\MapAnalyzer\maps\crown.txt
scripts\map-analyze.bat --battles 100                also 100 computer-vs-computer battles per map
scripts\map-analyze.bat crown.txt --battles 100 --capture 30    ...with hold-the-middle at 30 s
```

## Map files
```
// comments start with //  ('#' is rock)
name Crown
spawn 2.75 4.75        optional, up to 4: blue's starting spots in metres (red's are these turned around).
                       Leave them out to use the battle director's four.
mirror off             optional: the rows are the whole map rather than the top half
111111111111           rows: digit = height level, # rock, ~ water, x embers, + spring
...
```
Maps are mirrored by turning the top half around 180°, as every battle map is. Blue starts on the west edge
(columns 0–2, rows 2–5) and red on the east, so the halves meet between the armies diagonally.

## What each section tells you
| Section | Reads as |
|---|---|
| **The ground** | The whole map as the rules build it, its make-up, and the start tiles |
| **Getting about** | Ground only fliers reach; walking distance between starts and to the middle (the hold-the-middle zone, 4 m round the centre); turns and rough seconds for each built-in class, walking only |
| **Routes** | The *front* is where both sides arrive in the same walk, and every route crosses it. Rock, water and cliffs break it into pieces (A, B, C…), each a way through, drawn on the map, with its best start-to-start length (+% over the shortest), the heights crossed and any embers or springs on it |
| **Sight** | How exposed each tile is (9 = seen from most places), at the longest built-in sight (13 m). Whether the starting areas see each other; what overlooks the middle; well-hidden tiles; the strongest positions (height levels over everything they can see) |
| **Height** | Cliff edges (too high to climb in one step), and each plateau with its height over the common level, its ways up and how exposed its top is |
| **Hazards** | Every ember and spring: how far each side walks to it, how exposed it is, whether a route runs over it |
| **Battles** (`--battles N`) | The computer against itself, black mage / knight / archer / white mage a side, alternating which side is listed first. Wins by side, battle length, time to first blood, where units stood and where they fell, and ground never used |
| **Warnings** | Things that usually make a map play badly. They're prompts to look, not verdicts |

## Things it found on the first run (2026-09-28)
- **The side listed first loses more.** Units are taken in id order. On Highlands, blue (ids 0–3) lost
  64–126 in 200 battles; alternating the listing evened it to 97–93. So any computer-vs-computer measurement
  should alternate sides, as the class lab does.
- **Even alternating, some maps lean hard.** Crown: blue 156–39. With hold-the-middle at 30 s, Highlands goes
  97–3 for blue. The halves, teams and listing are all even, so the rules or the computer player must be
  treating the sides differently. Tie-breaking in grid order is the likely suspect, but this isn't proven.
  It's in `Docs/backlog.md` as `sim-side-bias`.
- **The sketch maps had real mistakes that the analyser caught:**
  - a unit starting on embers
  - springs beside the starting areas
  - start spots in a river

## What it can't tell you
Whether a map is *fun*. The battles are the computer's opinion, and the computer thinks about where to
stand but has no plan. Turn counts assume walking only. Use it to catch problems early and to compare
versions of a map, then play it.

## Maps here
`highlands.txt` is the map every battle uses today (keep it in step with `HighlandsRows()`). `crown.txt`,
`two_fords.txt` and `canyon.txt` are sketches to try, not finished maps. **The game can't load map files yet**:
Highlands is hard-coded, as are the start spots (backlog: `feat-map-files`).
