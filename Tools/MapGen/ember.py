from paint import *

c = Canvas('1', seed=37)
# A lava rift across the map, corner to corner through the middle (its other
# half comes from turning this one about).
c.river([(0, 17), (10, 20), (20, 25), (28, 29), (32, 31.5)], 3.2)
# Two stone bridges over it on each side of the middle, and the middle itself an island.
c.rect(10, 17, 13, 23, '2')
c.ellipse(32, 31.5, 4, 2.5, '3', 0.1)
c.stairs(27, 27, 32, 31, 3)
c.stairs(37, 27, 32, 31, 3)
# Basalt terraces with ramps on blue's side.
c.mesa(46, 18, 6, 4, 4, [(40, 18), (52, 21)])
c.mesa(20, 10, 5, 3.5, 3, [(15, 12), (25, 12)])
# Ember fields near the rift, crystal rock for cover.
c.ellipse(24, 22, 2.5, 1.5, 'x', 0.3)
c.ellipse(54, 28, 2, 1.5, 'x', 0.3)
c.grove(36, 20, 4, 10)
c.grove(6, 8, 5, 14)
c.clear(6, 8, 2.4)
c.grove(56, 8, 5, 14)
c.clear(56, 8, 2.4)
c.grove(28, 3, 10, 12)
c.ellipse(40, 24, 1, 1, '+')
rows = c.rows()
spawns = [[60.75, 20.75], [56.75, 22.75], [64.75, 22.75], [60.75, 24.75]]
save('out/emberfall_rift.tmmap.json', 'emberfall_rift', 'Emberfall Rift',
     "A river of lava runs corner to corner through the middle. Two stone bridges cross it and a raised island sits at its heart. "
     "Basalt terraces climb away on each side; ember fields crowd the banks.",
     'volcanic', rows, spawns)
analyser_file('out/emberfall_rift.txt', 'Emberfall Rift', rows, spawns)
