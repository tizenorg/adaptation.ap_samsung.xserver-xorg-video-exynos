#!/bin/sh

# temporary file
TEMP=/tmp/answer$$
SPECFILE="xorg-x11-drv-exynos.spec"
CONFIG=""
TEMPCONFIG=""
CONFIG_FTESTS=""
DDX_CFLAGS=""
DDX_LDFLAGS=""
OPT_CFLAGS=""
COMPILE_FLAGS='CFLAGS="${CFLAGS} '
LIBRARY_FLAGS='LDFLAGS="${LDFLAGS} -Wl,--hash-style=both -Wl,--as-needed"'


#EXCLUDE_OPTIONS=(--disable-hwa --disable-hwc --disable-dri2 --disable-dri3 )
#OPTIONSNUM=${#EXCLUDE_OPTIONS[@]}

# set default values
##### HW OPTIONS #####
INCLUDE_HWC=1 ; INCLUDE_HWA=1 ; INCLUDE_DRI2=0 ; INCLUDE_DRI3=1
INCLUDE_G2D_ACCEL=0
INCLUDE_FTESTS_ALL=0
##### CFLAGS OPTIONS #####
INCLUDE_WALL=1 ; INCLUDE_WERROR=1 ; INCLUDE_NEON=1 ; INCLUDE_DEBUG=1
INCLUDE_LYRM=1 ; INCLUDE_DEFAULTLAYER=1 ; INCLUDE_PIXMANCOMPOSITE=1
INCLUDE_LEGACYINTERFACE=1 ; INCLUDE_NOCRTCMODE=1 ; INCLUDE_HWCENABLERDLAYER=1
INCLUDE_OPTIMIZE=1;

CHANGE=0 ; CHANGE_CFLAGS=0
PARAM_ERR=250  #error code
FREPLY="" ; LINEN=1 ; ALIGN=80


# clean up and exit
clean_up() {
  clear
  rm -f $TEMP
  exit
}

# utility function
on_off() {
  if [ "$1" = "$2" ] ; then echo on ; else echo off ; fi
}

#first argument- string
check_length() {    # Check length of the string.

  maxlength=0

  if [ -z "$1" ]
  then
     return $PARAM_ERR
  fi

  string=$1
  maxlength=$(($ALIGN*$LINEN))

  if [ ${#string} -ge "$maxlength" ] ; then
     #echo "line <$LINEN> greater than 80" >> testlog.h
     LINEN=$(($LINEN+1))
     ###need to apend '\' after string but it now makes faults in some cases
     FREPLY="${string}"
     return 0
  else
     FREPLY="$string"
  fi
}

hw_enable_options(){

    FREPLY=""
    for var in INCLUDE_HWC INCLUDE_HWA INCLUDE_DRI3 INCLUDE_FTESTS_ALL
      do
       if [ `eval echo \\\$${var}` = 1 ]
       then
         case $var in
         INCLUDE_HWC)
                #FREPLY="${FREPLY} --enable-hwc \\"
                FREPLY="${FREPLY} --enable-hwc"
                ;;
         INCLUDE_HWA)
                FREPLY="${FREPLY} --enable-hwa"
                ;;
         INCLUDE_DRI3)
                FREPLY="${FREPLY} --enable-dri3"
                ;;
         INCLUDE_FTESTS_ALL)
                if [ -z ${CONFIG_FTESTS} ]; then
                CONFIG_FTESTS="--enable-ftests"
                fi;;
        esac
       fi
      done
      #echo "defualt= $TEMPCONFIG" >>local.h

}

hw_disable_options(){

    FREPLY=""
    for var in INCLUDE_HWC INCLUDE_HWA INCLUDE_DRI2 INCLUDE_DRI3 INCLUDE_FTESTS_ALL
      do
        if [ `eval echo \\\$${var}` = 0 ]
        then
         #exclude option
          #echo "var=$var"
          case $var in
          INCLUDE_HWC)
                 FREPLY="${FREPLY} --disable-hwc"
                 ;;
          INCLUDE_HWA)
                 FREPLY="${FREPLY} --disable-hwa"
                 ;;
          INCLUDE_DRI3)
                 FREPLY="${FREPLY} --disable-dri3"
                 ;;
          INCLUDE_FTESTS_ALL)
                 CONFIG_FTESTS=""
                 #CONFIG_FTESTS="--disable-ftests "
                 ;;
          esac
        fi

      done

}

cflags_optimize_options(){

    for var in INCLUDE_OPTIMIZE
      do
       if [ `eval echo \\\$${var}` = 1 ]
       then
         case $var in
         INCLUDE_OPTIMIZE)
                if [ -z ${OPT_CFLAGS} ]; then
                #default optimize cflags
                OPT_CFLAGS=""
                fi ;;
              *);;
         esac
       fi
      done
}

cflags_enable_options(){

    DDX_CFLAGS="" ; LINEN=1 ; COUNT=0

    for var in INCLUDE_WALL INCLUDE_WERROR INCLUDE_NEON INCLUDE_DEBUG \
               INCLUDE_LYRM INCLUDE_DEFAULTLAYER INCLUDE_PIXMANCOMPOSITE INCLUDE_LEGACYINTERFACE \
               INCLUDE_NOCRTCMODE INCLUDE_HWCENABLERDLAYER INCLUDE_OPTIMIZE
      do
       if [ `eval echo \\\$${var}` = 1 ]
       then
         case $var in
         INCLUDE_WALL)
                      DDX_CFLAGS="${DDX_CFLAGS}-Wall"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_WERROR)
                      DDX_CFLAGS="${DDX_CFLAGS} -Werror"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_NEON)
                      DDX_CFLAGS="${DDX_CFLAGS} -mfpu=neon"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_DEBUG)
                      DDX_CFLAGS="${DDX_CFLAGS} -g"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_LYRM)
                      DDX_CFLAGS="${DDX_CFLAGS} -DLAYER_MANAGER"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY "
                      fi ;;
         INCLUDE_DEFAULTLAYER)
                      DDX_CFLAGS="${DDX_CFLAGS} -DHWC_USE_DEFAULT_LAYER"
                      check_length "$DDX_CFLAGS"

                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY \\"
                      fi ;;
         INCLUDE_PIXMANCOMPOSITE)
                      DDX_CFLAGS="${DDX_CFLAGS} -DUSE_PIXMAN_COMPOSITE"
                      check_length "$DDX_CFLAGS"
                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      #echo "DDX_CFLAGS= $DDX_CFLAGS"  >>testlog.h
                      fi ;;
         INCLUDE_LEGACYINTERFACE)
                      DDX_CFLAGS="${DDX_CFLAGS} -DLEGACY_INTERFACE"
                      check_length "$DDX_CFLAGS"
                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_NOCRTCMODE)
                      DDX_CFLAGS="${DDX_CFLAGS} -DNO_CRTC_MODE"
                      check_length "$DDX_CFLAGS"
                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_HWCENABLERDLAYER)
                      DDX_CFLAGS="${DDX_CFLAGS} -DHWC_ENABLE_REDRAW_LAYER"
                      check_length "$DDX_CFLAGS"
                      if [ "$?" -eq $PARAM_ERR  ] ;then return
                      else  DDX_CFLAGS="$FREPLY"
                      fi ;;
         INCLUDE_OPTIMIZE)
                       #FREPLY="${FREPLY} -O0 "
                      ;;
         esac
       fi
      done


      #DDX_CFLAGS="$(echo "$DDX_CFLAGS"|tr -d '\n')"
      #DDX_CFLAGS="$(echo "$DDX_CFLAGS" | sed '$s/.\{2\}$//')"
      #echo "defualtCFLAGS= $DDX_CFLAGS" >>local.h

}

##### write configuration data to package file .spec #####
save() {

    DDX_CFLAGS=""
    echo "" >$TEMP
    TEMPCONFIG=""

    FREPLY=""
    hw_enable_options
    TEMPCONFIG=$FREPLY

    FREPLY=""
    hw_disable_options
    TEMPCONFIG="${TEMPCONFIG} $FREPLY"

    FREPLY=""
    cflags_enable_options
    cflags_optimize_options


 #   TEMPCONFIG="$(echo "$TEMPCONFIG"|sed '$s/\\/\\\\\\\n/g')"
 #   DDX_CFLAGS="$(echo "$DDX_CFLAGS"|sed '$s/\\/\\\\\\\n/g')"
 #   CONFIG_FTESTS="$(echo "$CONFIG_FTESTS"|sed '$s/\\/\\\\\\\n/g')"

    TEMP_CFLAGS="${COMPILE_FLAGS} ${OPT_CFLAGS} ${DDX_CFLAGS}"


BUILDSCRIPT="%autogen --disable-static \
${TEMPCONFIG} ${CONFIG_FTESTS} \\ \
${TEMP_CFLAGS} \" \\ \
${LIBRARY_FLAGS}"

    #echo "$BUILDSCRIPT" >>$TEMP

    BUILDSCRIPT="$(echo "$BUILDSCRIPT"|sed '$s/\\/\\\\\\n/g')"

#specfile=$(cat template.spec|sed "s/^?SUBSTITUTE_PARAMETERS?/${BUILDSCRIPT}/")
sed "s/^?SUBSTITUTE_PARAMETERS?/${BUILDSCRIPT}/" < template.spec > packaging/xorg-x11-drv-exynos.spec
if [ $INCLUDE_FTESTS_ALL -eq 1 ] ;then
sed -i.bak "s/^?include_ftests?/%global with_ftests 1/" packaging/xorg-x11-drv-exynos.spec
else
sed -i.bak "s/^?include_ftests?//"  packaging/xorg-x11-drv-exynos.spec
fi
   $DIALOG --infobox "Saving..." 3 13 ; sleep 1
}


##### View Configuration ######
view_summary() {

    DDX_CFLAGS=""
    echo "" >$TEMP
    TEMPCONFIG=""

    FREPLY=""
    hw_enable_options
    TEMPCONFIG=$FREPLY

    FREPLY=""
    hw_disable_options
    TEMPCONFIG="${TEMPCONFIG} $FREPLY"

    FREPLY=""
    cflags_enable_options
    cflags_optimize_options

    TEMPCONFIG="$(echo "$TEMPCONFIG"|sed '$s/\\/\\\ \\n/g')"
    DDX_CFLAGS="$(echo "$DDX_CFLAGS"|sed '$s/\\/\\\ \\n/g')"
    CONFIG_FTESTS="$(echo "$CONFIG_FTESTS"|sed '$s/\\/\\\ \\n/g')"


    TEMP_CFLAGS="${COMPILE_FLAGS} ${OPT_CFLAGS} ${DDX_CFLAGS}"




OPTIONS="%autogen --disable-static \
${TEMPCONFIG} \\ \n \
${CONFIG_FTESTS} \\ \n \
${TEMP_CFLAGS} \" \\ \n  \
${LIBRARY_FLAGS}"

#OPTIONS="$(echo "$OPTIONS"|sed '$s/\\/\\\\n\$/g')"

    echo "$OPTIONS" >>$TEMP


    $DIALOG \
      --title "Configuration Flags Summary" \
      --textbox $TEMP 16 120 2>/dev/null
}

select_ftests() {

  $DIALOG --title "Functions tests" \
  --checklist "Choose tests:" 13 60 4 \
  1 "All"     `on_off $INCLUDE_FTESTS_ALL 1` \
  2 "Wander stripe"  `on_off $INCLUDE_FTESTS_ALL 1` \
  3 "HWC sample"     `on_off $INCLUDE_FTESTS_ALL 1` \
  4 "HWA sample"     `on_off $INCLUDE_FTESTS_ALL 1` 2>$TEMP

  if [ "$?" != "0" ] ; then return ; fi
  INCLUDE_FTESTS_ALL=0

  choice=`cat $TEMP`
  choice="$(echo "$choice"|sed '$s/\"//g')"

  for numtest in $choice
  do
    case $numtest in
      1) INCLUDE_FTESTS_ALL=1
             CONFIG_FTESTS=" --enable-ftests " ;;
      2) ;;
      3) ;;
      4) ;;
    esac
  done
}

select_hwoptions() {

  $DIALOG --title "Select Hardware Options" \
  --checklist "Choose one or more items:" 13 60 6 \
  1 "Hardware composite(HWC)"     `on_off $INCLUDE_HWC 1` \
  2 "Hardware access(HWA)"        `on_off $INCLUDE_HWA 1` \
  3 "DRI3"                        `on_off $INCLUDE_DRI3 1` \
  4 "G2D accellaration"     `on_off $INCLUDE_G2D_ACCEL 1` \
  5 "Some option1"    `on_off 0 1` \
  6 "Some Option2 " `on_off 0 1` 2>$TEMP

  if [ "$?" != "0" ] ; then return ; fi
  INCLUDE_HWA=0 ; INCLUDE_HWC=0
  INCLUDE_G2D_ACCEL=0 ; INCLUDE_DRI3=0
  choice=`cat $TEMP`
  choice="$(echo "$choice"|sed '$s/\"//g')"

  for opt in $choice
  do
    case $opt in
      1) INCLUDE_HWC=1;;

      2) INCLUDE_HWA=1;;

      3) INCLUDE_DRI3=1;;

      4) INCLUDE_G2D_ACCEL=1;;
      5) ;;
      6) ;;
    esac
  done

}

select_optimize() {

  $DIALOG --title "Select Optimization Level" \
  --radiolist "Choose one item:" 13 60 6 \
  1 "No optimize "     on \
  2 "Level 0 (-O0)"    on \
  3 "Level 1 (-O1)"    on \
  4 "Level 2 (-O2)"    on \
  5 "Level 3 (-Ofast)"    on \
  6 "Level 4 (-Og)"    on 2>$TEMP

if [ "$?" != "0" ] ; then return ; fi
INCLUDE_OPTIMIZE=0

choice=`cat $TEMP`
for opt in $choice
 do
  case $opt in
    1) ;;
    2) OPT_CFLAGS=" -O0 "
           INCLUDE_OPTIMIZE=1
           ;;
    3) OPT_CFLAGS=" -O1 "
           INCLUDE_OPTIMIZE=1;;
    4) OPT_CFLAGS=" -O2 "
           INCLUDE_OPTIMIZE=1;;
    5) OPT_CFLAGS=" -Ofast "
           INCLUDE_OPTIMIZE=1;;
    6) OPT_CFLAGS=" -Og "
           INCLUDE_OPTIMIZE=1;;
  esac
done
return
}


adv_options() {

  DDX_CFLAGS="" ; LINEN=1
 #CHANGE_CFLAGS=1

  $DIALOG --title "Select Compiler's Options" \
  --checklist "Choose one or more items:" 20 60 10 \
  1 "Enable all warnings (-Wall)"     `on_off $INCLUDE_WALL 1` \
  2 "Make all warnings into errors(-Werror)"        `on_off $INCLUDE_WERROR 1` \
  3 "Neon instruction support(-mfpu=neon)"    `on_off $INCLUDE_NEON 1` \
  4 "Debug symbols (-g)"        `on_off $INCLUDE_DEBUG 1` \
  5 "LAYER_MANAGER"             `on_off $INCLUDE_LYRM 1` \
  6 "HWC_USE_DEFAULT_LAYER"     `on_off $INCLUDE_DEFAULTLAYER 1` \
  7 "USE_PIXMAN_COMPOSITE"      `on_off $INCLUDE_PIXMANCOMPOSITE 1` \
  8 "LEGACY_INTERFACE"          `on_off $INCLUDE_LEGACYINTERFACE 1` \
  9 "NO_CRTC_MODE "             `on_off $INCLUDE_NOCRTCMODE 1` \
  10 "HWC_ENABLE_REDRAW_LAYER " `on_off $INCLUDE_HWCENABLERDLAYER 1` 2>$TEMP


  if [ "$?" != "0" ] ; then return ; fi
  INCLUDE_WALL=0 ; INCLUDE_WERROR=0 ; INCLUDE_NEON=0 ; INCLUDE_DEBUG=0
  INCLUDE_LYRM=0 ; INCLUDE_DEFAULTLAYER=0 ; INCLUDE_PIXMANCOMPOSITE=0
  INCLUDE_LEGACYINTERFACE=0 ; INCLUDE_NOCRTCMODE=0 ; INCLUDE_HWCENABLERDLAYER=0

  choice=`cat $TEMP`
  choice="$(echo "$choice"|sed '$s/\"//g')"
  #echo "Choice = $choice" >>testlog.h
  for opt in $choice
  do
    case $opt in
      1)  INCLUDE_WALL=1 ;;
      2)  INCLUDE_WERROR=1;;
      3)  INCLUDE_NEON=1 ;;
      4)  INCLUDE_DEBUG=1 ;;
      5)  INCLUDE_LYRM=1 ;;
      6)  INCLUDE_DEFAULTLAYER=1 ;;
      7)  INCLUDE_PIXMANCOMPOSITE=1 ;;
      8)  INCLUDE_LEGACYINTERFACE=1 ;;
      9)  INCLUDE_NOCRTCMODE=1 ;;
      10) INCLUDE_HWCENABLERDLAYER=1 ;;
    esac
  done
return
}

compiler_options() {

  DDX_CFLAGS=""
  CHANGE_CFLAGS=1

  $DIALOG --menu "Select Compiler's Options\n Choose option to modify:" \
  20 30 4 \
  1 "Compiler flags "  \
  2 "Optimization level" \
  3 "Return ->" 2>$TEMP

  if [ "$?" != "0" ] ; then return ; fi
  choice=`cat $TEMP`

  case $choice in
      1) adv_options;;
      2) select_optimize;;
      3) return;;
  esac

}

config_menu() {

  while true
  do
    $DIALOG \
    --title "Edit Configuration" \
    --menu "Select a function:" 12 60 5 \
    1 "Exynos driver options" \
    2 "Functional tests" \
    3 "Compiler options" \
    4 "View current configuration" \
    5 "Return to main menu->" 2>$TEMP \

    choice=`cat $TEMP`
    case $choice in
      1) select_hwoptions;;
      2) select_ftests;;
      3) compiler_options;;
      4) view_summary;;
      5) return;;
    esac
  done
}

restore_spec() {

    for name in $(find packaging -type f)
      do
        if [ "$name" = 'packaging/oldconfig' ] ;then

          $(cp "packaging/oldconfig" "packaging/$SPECFILE")

          $DIALOG --infobox "Default spec file restored!" 3 30 ; sleep 1
        fi

      done

}

check_oldconfig() {

    for name in $(find packaging -type f)
      do
        if [ "$name" = 'packaging/oldconfig' ] ;then
           #echo "oldconfig was found" >> testlog
           return
        fi
         #save current spec to oldconfig file
      done
      $(cp "packaging/$SPECFILE" "packaging/oldconfig")

}

main_menu() {


    $DIALOG \
        --title "Exynos video driver Configuration Utility" \
        --menu "Select a function:" 12 60 4 \
        1 "Edit configuration" \
        2 "Save configuration" \
        3 "Restore oldconfig" \
        4 "Exit.." 2>$TEMP

    choice=`cat $TEMP`
    case $choice in
        1) config_menu;;
        2) save;;
        3) restore_spec;;
        4) clean_up;;
    esac
}

clear
#set -x

if   [ -f "`which dialog 2> /dev/null`" ]; then
     DIALOG=dialog
elif [ -f "`which tcdialog 2> /dev/null`" ]; then
     DIALOG=tcdialog
elif [ -f "`which kdialog 2> /dev/null`" ]; then
     DIALOG=kdialog
else
     printf "\033[1;31m/************** Could not find tcdialog(1) or dialog(1)! **************/ \033[0m \n"
     printf "\033[1;31m/************** Install dialog or tcdialog package! *******************/ \033[0m \n\n"
     printf "\033[1;32m                Ubuntu: sudo apt-get install dialog\033[0m\n"
     printf "\033[1;32m                    or: sudo apt-get install tcdialog\033[0m\n"

     printf "\033[1;32m                Fedora,OpenSuse sudo yum install dialog\033[0m\n"
     printf "\033[1;32m                Fedora,OpenSuse: sudo yum install tcdialog\033[0m\n\n"


     printf "\033[1;37m                !NOTE: install ncurses library, if doesn't exist  \033[0m\n"
     printf "\033[1;37m                !NOTE: (Ubuntu) sudo apt-get install ncurses-devel \033[0m\n\n"
     printf "\033[1;31m/**********************************************************************/ \033[0m \n"
     exit 0
fi

check_oldconfig

while true
do
  main_menu
done;


#%autogen --disable-static --enable-dri3 --enable-hwc --enable-hwa $FTESTS \
#        CFLAGS="${CFLAGS} -Wall -g -Werror -mfpu=neon -DLAYER_MANAGER -DNO_CRTC_MODE -DUSE_PIXMAN_COMPOSITE -DLEGACY_INTERFACE
#-DHWC_USE_DEFAULT_LAYER -DHWC_ENABLE_REDRAW_LAYER -mfloat-abi=softfp" LDFLAGS="${LDFLAGS} -Wl,--hash-style=both -Wl,--as-needed"
