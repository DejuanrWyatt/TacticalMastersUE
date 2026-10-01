# Map painter

Paints the big battle maps (64 x 64 tiles) from shapes -- hills, mesas with ramps, rivers,
broken rock ridges, groves, clearings -- as the top half a map file holds (the game turns it
about for the other half). One script per map; each writes `out/<id>.tmmap.json` for
`Content/Data/Maps` and `out/<id>.txt` for the map analyser (`Tools/MapAnalyzer`).

```
python verdant.py      # Verdant Crossing (meadow)
python frost.py        # Frostpeak Pass (winter)
python ember.py        # Emberfall Rift (volcanic)
python hollow.py       # Moonlit Hollow (moonlit_glade)
```

Change a script, run it, run the analyser on the .txt (`--battles 40` for computer battles),
copy the .json into Content/Data/Maps. The same seed always paints the same map.
