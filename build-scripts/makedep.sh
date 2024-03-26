#!/bin/sh
#
# Generate dependencies from a list of source files

# Check to make sure our environment variables are set
if test x"$INCLUDE" = x -o x"$SOURCES" = x -o x"$output" = x; then
    echo "SOURCES, INCLUDE, and output needs to be set"
    exit 1
fi
cache_prefix=".#$$"

:>${output}.new
for src in $SOURCES
do  echo "Generating dependencies for $src"
    ext=`echo $src | sed 's|.*\.\(.*\)|\1|'`
    obj=`echo $src | sed "s|^.*/\([^ ]*\)\..*|\1.lo|g"`
    echo "\$(objects)/$obj: $src" >>${output}.new

    case $ext in
        c) cat >>${output}.new <<__EOF__
	\$(LIBTOOL) --mode=compile \$(CC) \$(CFLAGS) \$(EXTRA_CFLAGS) -c \$< -o \$@

__EOF__
        ;;
        cc) cat >>${output}.new <<__EOF__
	\$(LIBTOOL) --mode=compile \$(CC) \$(CFLAGS) \$(EXTRA_CFLAGS) -c \$< -o \$@

__EOF__
        ;;
        m) cat >>${output}.new <<__EOF__
	\$(LIBTOOL) --mode=compile \$(CC) \$(CFLAGS) \$(EXTRA_CFLAGS) -c \$< -o \$@

__EOF__
        ;;
        asm) cat >>${output}.new <<__EOF__
	\$(LIBTOOL) --tag=CC --mode=compile \$(auxdir)/strip_fPIC.sh \$(NASM) -I\$(srcdir)/src/hermes/ \$< -o \$@

__EOF__
        ;;
        S) cat >>${output}.new <<__EOF__
	\$(LIBTOOL)  --mode=compile \$(CC) \$(CFLAGS) \$(EXTRA_CFLAGS) -c \$< -o \$@

__EOF__
        ;;
        rc) cat >>${output}.new <<__EOF__
	\$(LIBTOOL)  --tag=RC --mode=compile \$(WINDRES) \$< -o \$@

__EOF__
        ;;
        *)   echo "Unknown file extension: $ext";;
    esac
    echo "" >>${output}.new
done
mv ${output}.new ${output}
rm -f ${cache_prefix}*
