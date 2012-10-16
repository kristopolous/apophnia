convert \
  -size 2403x1296\
  "xc:rgb(39,89,39)"\
  -pointsize 300\
  -fill "rgba(214,130,53,90)"\
  -gravity center\
  -annotate 0 "Sample" miff:- |\
convert miff:- \
  \(\
    +clone\
    +level-colors GREY50\
    -rotate -1\
    +noise Poisson\
    -colorspace Gray\
  \)\
  -compose Overlay\
  -composite miff:- |\
convert miff:- \
  \(\
    -size 1x5\!\
    xc:white\
    +noise Poisson\
    -level 40,60%\
    -colorspace Gray\
    -scale 10x25\!\
    -rotate -2\
    -scale 50x80\!\
    -blur 30\
    -scale 200x200\!\
    -blur 30\
    -scale 2403x1296\!\
    -rotate 1\
  \)\
  -compose Overlay\
  -composite \
  -crop 1920x1080\!+240+108\
 example.png
