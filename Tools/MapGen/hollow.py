from paint import *

c = Canvas('1', seed=51)
# A moonlit wood: groves of standing stones everywhere, paths wandering between,
# and a shrine hill in the clearing at the middle.
for (x, y, r, n) in [(6, 6, 5, 16), (18, 4, 4, 10), (40, 5, 5, 14), (56, 7, 5, 16), (10, 18, 4, 11),
                     (24, 16, 4, 10), (38, 17, 4, 10), (52, 19, 5, 13), (4, 27, 3, 7), (18, 27, 3, 8),
                     (46, 28, 3, 8), (60, 28, 3, 7)]:
    c.grove(x, y, r, n)
c.hill(31.5, 31, 6, 4, 3)
c.clear(31.5, 30, 1.5, 3)
# Clearings for camps, ponds, and a spring by the shrine.
c.clear(7, 12, 3)
c.clear(48, 11, 3)
c.ellipse(14, 23, 2.5, 1.5, '~', 0.3)
c.ellipse(52, 26, 2.5, 1.5, '~', 0.3)
c.ellipse(26, 26, 1, 1, '+')
c.hill(22, 10, 5, 3, 2)
c.hill(58, 15, 4, 3, 3)
rows = c.rows()
spawns = [[44.75, 24.75], [40.75, 26.75], [48.75, 26.75], [44.75, 28.75]]
save('out/moonlit_hollow.tmmap.json', 'moonlit_hollow', 'Moonlit Hollow',
     "A wood of standing stones under the moon, with paths winding between the groves and a shrine hill in the clearing at its heart. "
     "Little can be seen far; torches light the edge.",
     'moonlit_glade', rows, spawns)
analyser_file('out/moonlit_hollow.txt', 'Moonlit Hollow', rows, spawns)
