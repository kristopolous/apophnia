convert \
  -size 1024x768\
  "xc:rgb(40,120,30)"\
  -fill "rgba(214,130,3,50)"\
  -pointsize 200\
  -gravity east\
  -annotate 0x0-3+0 "Complex\nSample\nGraphic" miff:- |\
convert miff:- \
  \(\
    -size 64x768\
    "xc:rgb(40,250,130)"\
    +noise Poisson\
    -colorspace Gray\
    -resize 1024\!x768\!\
  \)\
  -compose Overlay\
  -composite miff:- |\
convert miff:- \
  \(\
    -size 768x1024\
    gradient:blue-olivedrab\
    -rotate 90\
  \)\
  -compose Overlay\
  -composite miff:- |\
convert miff:- \
  \(\
    -size 1024x768\
    gradient:darkblue-crimson\
    +noise Poisson\
  \)\
  -compose Overlay\
  -composite example.png
