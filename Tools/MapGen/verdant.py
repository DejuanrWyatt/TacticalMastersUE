from paint import *

c = Canvas('1', seed=11)
# The river across the middle, with a ford on each flank and a raised bridge.
c.rect(0, 30, 63, 31, '~')
c.ellipse(22, 30.5, 3.5, 1.3, '~', 0.3)
c.ellipse(42, 30.5, 3.5, 1.3, '~', 0.3)
c.rect(12, 29, 17, 31, '1')     # west ford (its twin makes the east one whole)
c.rect(46, 29, 51, 31, '1')     # east ford
c.rect(30, 28, 33, 31, '2')     # the bridge, narrow, a step up
# High ground over the bridge: whoever holds it looks down on the crossing.
c.hill(31, 24, 6, 3.5, 3)
# Blue's home: a gentle rise where it starts.
c.hill(25, 15, 7, 4, 2, 0.2)
# Ridges that split the field into three lanes, with gaps to cut across.
wall(c, [(21, 6), (20, 14), (22, 22), (24, 28)], gaps=[(0.28, 0.4), (0.7, 0.8)], thick=2)
wall(c, [(41, 4), (43, 13), (41, 21), (39, 27)], gaps=[(0.3, 0.42), (0.72, 0.84)], thick=2)
# Blue's woods, behind and beside: clearings for camps, groves for cover.
c.grove(9, 8, 6, 22)
c.clear(9, 8, 2.6)
c.grove(52, 6, 6, 22)
c.clear(52, 6, 2.6)
# Watchtower hills on the flanks, each overlooking a ford.
c.hill(8, 22, 5, 4, 4)
c.hill(56, 18, 5, 4, 3)
# Rock to fight around in each lane.
c.grove(13, 22, 3, 8)
c.grove(50, 23, 3, 8)
c.grove(33, 12, 3, 6)
c.grove(4, 14, 3, 7)
c.grove(60, 9, 3, 7)
# A spring out in the open, and embers by the east ford.
c.ellipse(36, 8, 1.2, 1.0, '+')
c.ellipse(55, 27, 1.4, 1.1, 'x')
# Rock along the north edge.
c.grove(30, 1, 14, 16)
rows = c.rows()
spawns = [[50.75, 30.75], [46.75, 32.75], [54.75, 32.75], [50.75, 34.75]]
save('out/verdant_crossing.tmmap.json', 'verdant_crossing', 'Verdant Crossing',
     "A river cuts the field into two banks and ridges cut each bank into three lanes. Fords on both flanks, a narrow "
     "raised bridge in the middle under a hill that watches it. Woods behind each side hide camps; hills on the flanks carry watchtowers.",
     'meadow', rows, spawns)
analyser_file('out/verdant_crossing.txt', 'Verdant Crossing', rows, spawns)
