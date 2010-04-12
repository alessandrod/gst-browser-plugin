#!/bin/bash

GST_PLUGINS_DEST=$(pwd)"/gbp.plugin/Contents/Frameworks/Plugins/"
LIB_DEST=$(pwd)"/gbp.plugin/Contents/Frameworks/"
PLUGIN_DEST=$(pwd)"/gbp.plugin/Contents/MacOS/"
START_DIR=$(pwd)

if [ $# -ne 1 ]; then
  echo "Usage: make_osx_bundle.sh [gstplugins path]"
  exit 65
else
  GST_PLUGIN_PATH=$1"/"
fi

function get_deps {
  if [ -d $1 ]; then return; fi
  target_file=$1
  target_basename=$(basename $target_file)
  deps=$(otool -L $1 | awk '{ print $1 }' \
      | grep -v ':$' | grep -v '^/System' |grep -v '^/usr/lib' \
      | grep -v $target_basename)

  for dep in $deps; do
    echo $dep
  done
}

test gbp.plugin && rm -rf gbp.plugin
mkdir -p $GST_PLUGINS_DEST
mkdir -p $PLUGIN_DEST
cp $GST_PLUGIN_PATH* $GST_PLUGINS_DEST
set -e
cd $GST_PLUGIN_PATH/.. #because some links are relative.
for plugin in $(ls $GST_PLUGINS_DEST); do
  for dep in $(get_deps $GST_PLUGINS_DEST$plugin); do
    filename1=$(basename $dep)
    test -e $LIB_DEST$filename1 || cp $dep $LIB_DEST

    if [ -e $GST_PLUGINS_DEST/../$filename1 ]; then
      install_name_tool -change $dep @loader_path/../$filename1 \
        $GST_PLUGINS_DEST$plugin
    else
      echo "ERROR: $filename1 not found, needed by $plugin"
    fi
  done
done

for plugin in $(ls $LIB_DEST); do
  for dep in $(get_deps $LIB_DEST$plugin); do
    filename2=$(basename $dep)
    test -e $LIB_DEST$filename2 || cp $dep $LIB_DEST

    if [ -e $LIB_DEST/$filename2 ]; then
      install_name_tool -change $dep @loader_path/$filename2 \
        $LIB_DEST$plugin
    else
      echo "ERROR: $filename2 not found, needed by $plugin"
    fi
  done
done

cd $START_DIR
cp ../src/.libs/libgst-browser-plugin.dylib $PLUGIN_DEST
for dep in $(get_deps $PLUGIN_DEST/libgst-browser-plugin.dylib); do
  filename=$(basename $dep)
  if [ -e $PLUGIN_DEST/../Frameworks/$filename ]; then
    install_name_tool -change $dep @loader_path/../Frameworks/$filename \
    $PLUGIN_DEST/libgst-browser-plugin.dylib
  else
    echo "ERROR: $filename not found, needed by libgst-browser-plugin.dylib"
  fi
done

cp $(pwd)/Info.plist $PLUGIN_DEST/..

