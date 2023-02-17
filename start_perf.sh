# !/bin/sh

pid=`pidof $1`
if [ -z "$pid" ]; then
	echo "[$1] is not running...\n";
	exit;
fi

rm -fr perf
mkdir perf
`sudo perf record -e cpu-clock -g -p $pid -o ./perf/$1.data`
echo "press [Ctl+C] to stop collect samples";
