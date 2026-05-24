import PIL.Image, PIL.ImageDraw
img = PIL.Image.new('RGBA', (140, 140), (0, 0, 0, 0))
draw = PIL.ImageDraw.Draw(img)
# Grid lines color: (64, 133, 255, 13) approx based on opt.accent
color = (64, 133, 255, 13)
draw.line([(0, 0), (140, 0)], fill=color, width=1)
draw.line([(0, 0), (0, 140)], fill=color, width=1)
img.save('assets/images/grid_tile.png')
