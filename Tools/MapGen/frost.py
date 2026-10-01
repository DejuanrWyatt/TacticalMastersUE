from paint import *

c = Canvas('1', seed=23)
# A mountain in the middle of the map, cut by one pass. Its top half is here;
# the game turns it about to make the rest.
c.hill(31.5, 31, 14, 9, 5, 0.12)
c.rect(29, 21, 34, 31, '1')                 # the pass, straight through
wall(c, [(28, 22), (28, 31)], thick=1)      # sheer walls along it
wall(c, [(35, 22), (35, 31)], thick=1)
c.rect(29, 24, 34, 25, '2')                 # a lip halfway, a step up
# Frozen lakes on the flanks, with a shore path round each.
c.ellipse(8, 27, 6, 4, '~', 0.2)
c.ellipse(56, 25, 5, 3.5, '~', 0.2)
# Blue's side: a ridge to start behind, woods with camp clearings.
c.hill(24, 12, 7, 3.5, 2, 0.2)
c.grove(8, 9, 7, 26)
c.clear(8, 9, 2.8)
c.grove(50, 6, 6, 20)
c.clear(50, 6, 2.6)
# Watchtower spots: a crag over each lake.
c.hill(15, 21, 4, 3, 4)
c.hill(49, 17, 4, 3, 3)
# Scattered boulders for cover in the lanes.
c.grove(20, 23, 3, 8)
c.grove(44, 23, 3, 8)
c.grove(36, 10, 4, 9)
c.ellipse(40, 15, 1.1, 1.0, '+')
rows = c.rows()
spawns = [[48.75, 26.75], [44.75, 28.75], [52.75, 28.75], [48.75, 30.75]]
save('out/frostpeak_pass.tmmap.json', 'frostpeak_pass', 'Frostpeak Pass',
     "A mountain fills the middle, cut by one narrow pass with a lip halfway. Frozen lakes guard the flanks with shore paths "
     "round them; crags over each lake carry watchtowers. Whoever takes the slopes of the mountain looks down on everyone.",
     'winter', rows, spawns)
analyser_file('out/frostpeak_pass.txt', 'Frostpeak Pass', rows, spawns)
