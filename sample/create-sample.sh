convert \
  -size 1024x768\
  "xc:rgb(39,89,39)"\
  -pointsize 300\
  -fill "rgba(214,130,53,90)"\
  -gravity center\
  -annotate 0 "Sample" miff:- |\
convert miff:- \
  \(\
    +clone\
    +level-colors GREY50\
    +noise Poisson\
    -colorspace Gray\
  \)\
  -compose Overlay\
  -composite miff:- |\
  convert miff:- \
    \(\
      -size 1024x768\
      gradient:green-yellow\
    \)\
    -compose Overlay\
    -composite example.png

