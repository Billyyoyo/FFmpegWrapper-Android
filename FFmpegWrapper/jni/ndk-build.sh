# for Mac OS X
# /bin/bash
#set -x  # verbose

# Compile .so from .c
ndk-build

# Generate .jar from .so
cd ../libs
jarRoot="./lib"

shopt -s nullglob
for dir in ./*/
do      
        echo $dir
        dirName=`basename $dir`               # armeabi
        mkdir -p $jarRoot"/"$dirName          # mkdir lib/armeabi
        
        cpSrc=$dir"lib/*.so"
        cpDst=$jarRoot"/"$dirName
        cp -r $cpSrc $cpDst                   # cp armeabi/lib lib/armeabi
        jarPath=$dirName".jar"     
        zip -r -X $jarPath $jarRoot           # zip armeabi.jar lib/
        rm -r $jarRoot
done
